#pragma once

#include "TetrisManager.h"
#include "TetrisComponents.h"

#include "Forge/Scene/Entity.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Forge {
	class Scene;
}

namespace Tetris {

// Presents the game into an engine Scene.
//
// The rules live in TetrisManager and know nothing about entities; this is the
// only place the two meet. It mirrors the manager's state onto a handful of
// components (for tools and agents to read) and onto one mesh entity per
// occupied cell (for the renderer to draw).
// Shared material tuning so the board and the preview stay visually identical.
// Painted metal plate, not a mirror: the albedo map has to survive into the
// diffuse term for the tile to be *on* the block, and pbr.frag zeroes diffuse
// at full metallic (`kD = (1 - kS) * (1 - metallic)`). At metallic 1 the map
// only tints a razor-sharp glint, so the tile's colour appears wherever the
// reflection happens to land - on some blocks and not others - which is the
// art disappearing into the field rather than covering it. A modest metallic
// keeps the metal character in the specular; a roughness that spreads the
// highlight lets the whole face read as the surface the artist drew.
//
// 3D form: pbr.frag adds emissive as a flat term (`+ emissive`, independent of
// NdotL), so a strong self-glow lifts the shadowed faces toward the lit ones and
// the directional shading that reads as depth collapses into a uniform slab.
// Keeping emissive a faint rim keeps the lit/unlit face contrast that makes each
// block read as a solid, and feeds far less energy into the bloom pass. A
// tighter roughness with a touch more metallic gives the top faces and the gaps
// between blocks a crisp edge glint, which is where the 3D shape is legible.
//
// AO is the lever on the ambient/IBL lift. pbr.frag multiplies both the flat
// ambient and the diffuse IBL by AO, and the IBL irradiance is a bright neutral
// environment, so AO=1 paints every face - including the ones turned away from
// the key light - at full albedo. That is the pastel wash: no face is dark, so
// nothing reads as a solid with a lit side and a shadowed side. Pulling AO down
// darkens the ambient term, the unlit faces fall into shadow, and the saturated
// albedo survives on the lit faces instead of being lifted toward white.
constexpr float kBlockMetallic = 0.55f;
constexpr float kBlockRoughness = 0.22f;
constexpr float kBlockEmissiveIntensity = 0.04f;
constexpr float kBlockAO = 0.55f;

// Albedo is white because the theme's art supplies the colour.
//
// pbr.frag computes `albedo.rgb * texture(u_AlbedoTexture, vUV).rgb`, so a
// coloured albedo multiplies into the texture and the two hues muddy into
// something neither of them was. White hands the decision to the texture
// alone, which is what makes the art the theme rather than a tint over one.
constexpr glm::vec3 kBlockAlbedo{ 1.0f };

// Pieces are distinguished by colour alone (emissive + albedo), no textures.

class BoardView
{
public:
	BoardView() = default;
	~BoardView();

	BoardView(const BoardView&) = delete;
	BoardView& operator=(const BoardView&) = delete;

	// Attach to a scene and create the state entities. Must succeed before
	// Update() does anything; returns false when there is no scene, so a caller
	// that ignored a failure gets a game that quietly never starts rather than
	// a crash.
	//
	// The scene is a plain pointer, and deliberately so: the view lives inside
	// the game's script, which lives inside the scene, so the scene outlives
	// it by construction. A shared pointer would need the scene to own
	// itself, and a scene that owns itself is a cycle.
	bool Initialize(Forge::Scene* scene, const std::string& blockMeshPath = "resources/models/cube.glb");

	// Start the game, advance it by dt, and mirror the result into the scene.
	void Update(float dt);

	// Forward a player action to the rules.
	void HandleAction(Action action, bool pressed);

	// Restart from an empty board.
	void Restart();

	// Destroy every entity this view created.
	void Shutdown();

	bool IsInitialized() const { return m_Initialized; }
	TetrisManager& GetManager() { return m_Manager; }
	const TetrisManager& GetManager() const { return m_Manager; }
	Forge::Scene* GetScene() const { return m_Scene; }

	// --- Playfield placement, in world units ---
	//
	// The board's bottom-left corner, so cell (0,0) sits one block up and to
	// the right of it. Exposed because the camera and the backdrop have to
	// agree with the blocks, and hard-coding that in three places is how they
	// drift apart.
	static glm::vec3 BoardOrigin();
	static glm::vec3 BoardCentre();
	static glm::vec3 BoardSize();
	static float BlockSize();

	// World position of the centre of grid cell (col, row).
	static glm::vec3 CellToWorld(uint32_t col, uint32_t row);

	// Offsets of a tetromino's four preview blocks, relative to the preview
	// root. The shape is laid out at rotation 0, its bounding box centred in
	// the 4x4 tray, and rows flipped the way the playfield flips them, so the
	// preview shows the piece the way it spawns - not mirrored into a corner.
	static void PreviewLocalOffsets(TetrominoType type, glm::vec3 outOffsets[4]);

	// A lock as the view saw it. The rules lock a piece in one step; this says
	// how far it fell and how fast it was moving when it stopped, which is what
	// the landing animation and the impact shake are made of.
	struct Impact
	{
		bool Occurred = false;
		float Rows = 0.0f;           // cells fallen in the step that locked
		float Compact = 0.0f;        // cells the landing blocks slid as the stack packed
		float Speed = 0.0f;          // cells per second at the moment of impact
		int LinesCleared = 0;        // lines removed by this lock
		int ClearedLines = 0;        // alias for LinesCleared
		glm::ivec2 Landing = glm::ivec2(0, 0);  // piece origin it locked at
	};

	// The impact of the most recent Update, cleared by each one so a caller
	// cannot act twice on the same landing.
	const Impact& GetLastImpact() const { return m_LastImpact; }

	// Rows removed by the most recent lock, in ascending order. Empty when no
	// line was cleared this frame.
	const std::vector<uint32_t>& GetLastClearedRows() const { return m_LastClearedRows; }

private:
	// Which curve carries a block from From to To.
	enum class Curve : uint8_t
	{
		Slide,  // EaseInOut: a block moving to a cell the player chose
		Strike, // StrikeEase: a block dropped into the board
		Glide,  // constant rate: a block falling at the drop rate
	};

	// One block easing from where it is drawn to where the board says it is.
	struct Motion
	{
		glm::vec3 From = glm::vec3(0.0f);
		glm::vec3 To = glm::vec3(0.0f);
		float Elapsed = 0.0f;
		float Duration = 0.0f;
		Curve CurveType = Curve::Slide;

		// Seconds before the motion starts. The stack above a cleared row has
		// to wait for the row to go; a block sliding through a row that is
		// still animating away is two animations occupying the same cell, and
		// the clear reads as the stack moving through a fading wall.
		float Delay = 0.0f;
	};

	// The state and the drawn positions at the end of the previous Update. The
	// input layer drives the manager directly, so a hard drop can lock a piece
	// between two Updates; the only way the view can see the fall it has to
	// animate is to remember what it drew last.
	struct Snapshot
	{
		std::vector<uint8_t> Board;
		TetrisPiece Piece;
		glm::vec3 SlotPosition[4];
		bool Valid = false;
	};

	// Copy score, level and piece data onto the state components.
	void SyncStateComponents();

	// Create, move and destroy block entities so they match the locked board.
	void SyncBoardEntities();

	// The falling piece, drawn as four blocks of its own that ease between cells.
	void SyncPieceEntities();

	// Write every in-flight motion onto its entity, and retire the finished ones.
	void AdvanceMotions(float dt);

	// Animate blocks in cleared rows shrinking away, then destroy them.
	void AdvanceClearAnimations(float dt);

	// Hand the blocks a fire block just melted to the shrink-away animation,
	// burning toward the fire's colour. Runs before the sweep that destroys
	// blocks the board no longer holds, so a melted block animates out instead
	// of vanishing in one frame.
	void SeedMeltAnimations();

	// Measure the landing that just happened, if any.
	void DetectImpact(float dt);

	// Remember the state and the drawn positions for the next Update.
	void TakeSnapshot();

	// Entities created by Initialize, torn down by Shutdown.
	void DestroyEntity(Forge::Entity& entity);
	void DestroyPieceBlocks();

	// A block that moves to a new cell has to carry its name with it, because
	// the name is where the block is: a tool reading the scene by name, and the
	// tests that say what the player sees, both find a block by its cell.
	void RenameEntity(Forge::Entity entity, const std::string& name);

	// Grid cell -> entity handle, so a cell keeps its entity between frames
	// instead of being destroyed and rebuilt every tick.
	static uint64_t CellKey(uint32_t col, uint32_t row);

	// Slot keys for the falling piece, kept out of the cell-key space.
	static constexpr uint64_t kSlotKeyBase = 0x1000000000000000ULL;
	static uint64_t SlotKey(uint32_t slot);

	// The scene the board's entities live in. See Initialize() for why this
	// is a plain pointer.
	Forge::Scene* m_Scene = nullptr;
	TetrisManager m_Manager;

	Forge::Entity m_GameEntity;     // TetrisGameComponent
	Forge::Entity m_BoardEntity;    // TetrisBoardComponent
	Forge::Entity m_PieceEntity;    // TetrisPieceComponent
	Forge::Entity m_DisplayEntity;  // TetrisDisplayComponent

	std::map<uint64_t, Forge::Entity> m_Blocks;

	// The falling piece: four entities with their own identity, so a move
	// animates the same block into its next cell instead of destroying it and
	// building a new one there.
	Forge::Entity m_PieceBlocks[4] = {};
	glm::vec3 m_SlotPosition[4] = {};

	// A block in a cleared row, travelling into the row it filled and then
	// shrinking out of it. The blocks the player just placed have no board cell
	// to be found in - the row is removed in the same step the piece locked -
	// so they need this record to be part of the animation at all.
	struct ClearingBlock
	{
		Forge::Entity Entity;
		glm::vec3 Color = glm::vec3(1.0f);

		// Where the block was drawn when the line filled, and the cell it
		// filled. A block already in the row has them equal, so it only shrinks.
		glm::vec3 From = glm::vec3(0.0f);
		glm::vec3 To = glm::vec3(0.0f);

		// The two phases share one clock: travel, then flash and shrink.
		float TravelDuration = 0.0f;
		float Elapsed = 0.0f;
		float Duration = 0.0f;

		// Seconds before the clear starts. A hard drop packs the stack on the
		// way in, and the rows the compaction completes are the rows that
		// clear: the stack has to arrive before the row it filled goes out.
		float Delay = 0.0f;

		// A melting block burns toward the fire's colour; a cleared row flashes
		// white. Same animation, different glow.
		bool IsMelt = false;
	};

	std::map<uint64_t, Motion> m_Motions;
	std::map<uint64_t, ClearingBlock> m_ClearingBlocks;
	Snapshot m_Snapshot;
	Impact m_LastImpact;

	// Rows the clear removed in the frame that just locked, in ascending order.
	// Derived from the snapshot, because the board is cleared in the same step
	// that writes the piece and nothing records the row it was full in.
	std::vector<uint32_t> m_LastClearedRows;

	// Seconds between the last two downward steps of the falling piece, which
	// is the rate the fall has to be drawn at. Measured, not read from the drop
	// interval, because soft drop falls faster than gravity and a glide pinned
	// to the gravity rate would lag the key.
	float m_FallCadence = 0.0f;
	float m_TimeSinceFall = 0.0f;

	// Clock driving the fire block's flashing glow, so the pulse is continuous
	// across frames and resets when a new piece spawns.
	float m_FireFlash = 0.0f;

	// Cells that should exist this frame, with the colour to draw them in.
	// Reused between frames so a full board does not allocate 200 map nodes
	// sixty times a second.
	std::map<uint64_t, glm::vec3> m_Occupied;

	std::string m_BlockMeshPath;

	bool m_FirstFrame = true;
	bool m_Initialized = false;
};

}  // namespace Tetris