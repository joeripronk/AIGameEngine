#pragma once

#include "TetrisManager.h"
#include "TetrisComponents.h"

#include "Forge/Scene/Entity.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

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
constexpr float kBlockMetallic = 0.35f;
constexpr float kBlockRoughness = 0.45f;
constexpr float kBlockEmissiveIntensity = 0.06f;

// Albedo is white because the theme's art supplies the colour.
//
// pbr.frag computes `albedo.rgb * texture(u_AlbedoTexture, vUV).rgb`, so a
// coloured albedo multiplies into the texture and the two hues muddy into
// something neither of them was. White hands the decision to the texture
// alone, which is what makes the art the theme rather than a tint over one.
constexpr glm::vec3 kBlockAlbedo{ 1.0f };

// One surface per tetromino.
//
// Every tile is a crop of "Colored Metal tiles 1b" by sgtcrispy (CC0, public
// domain) - a sheet of eight colour palettes crossed with six metal patterns:
// diamond plate, a solid plate, panels, rivets, a grate and slats. Each piece
// takes one tile, so the art supplies a colour *and* a pattern, and the
// pattern is what still tells two pieces apart when the lighting flattens
// their hues.
//
// The sheet has no yellow or orange palette, so these are not the classic
// cyan/yellow/magenta/green/red/blue/orange set; `TetrominoDefinition::color`
// was retuned to each tile's own dominant colour, and that colour is what the
// emissive term, the preview and the state components use.
constexpr struct BlockTheme
{
	const char* Texture;
} kBlockTheme[static_cast<size_t>(TetrominoType::Count)] = {
	{ "resources/textures/tetris/block_i_diamond_plate.png" },  // I  teal,    diamond plate
	{ "resources/textures/tetris/block_o_solid_plate.png" },    // O  magenta, solid plate
	{ "resources/textures/tetris/block_t_panels.png" },         // T  pink,    panels
	{ "resources/textures/tetris/block_s_rivets.png" },         // S  green,   rivets
	{ "resources/textures/tetris/block_z_grate.png" },          // Z  crimson, grate
	{ "resources/textures/tetris/block_j_gold_slats.png" },      // J  blue,    slats
	{ "resources/textures/tetris/block_l_diamond_plate.png" },  // L  indigo,  diamond plate
};

// The surface a piece is drawn with. An out-of-range type falls back to the
// first entry rather than indexing past the end of the table.
inline const char* GetBlockTexture(TetrominoType type)
{
	const size_t index = static_cast<size_t>(type);
	return kBlockTheme[index < static_cast<size_t>(TetrominoType::Count) ? index : 0].Texture;
}

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
	bool Initialize(Forge::Ref<Forge::Scene> scene, const std::string& blockMeshPath = "resources/models/cube.glb");

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
	Forge::Ref<Forge::Scene> GetScene() const { return m_Scene; }

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
		float Speed = 0.0f;          // cells per second at the moment of impact
		glm::ivec2 Landing = glm::ivec2(0, 0);  // piece origin it locked at
	};

	// The impact of the most recent Update, cleared by each one so a caller
	// cannot act twice on the same landing.
	const Impact& GetLastImpact() const { return m_LastImpact; }

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

	// Measure the landing that just happened, if any.
	void DetectImpact(float dt);

	// Remember the state and the drawn positions for the next Update.
	void TakeSnapshot();

	// Entities created by Initialize, torn down by Shutdown.
	void DestroyEntity(Forge::Entity& entity);
	void DestroyPieceBlocks();

	// Grid cell -> entity handle, so a cell keeps its entity between frames
	// instead of being destroyed and rebuilt every tick.
	static uint64_t CellKey(uint32_t col, uint32_t row);

	// Slot keys for the falling piece, kept out of the cell-key space.
	static constexpr uint64_t kSlotKeyBase = 0x1000000000000000ULL;
	static uint64_t SlotKey(uint32_t slot);

	Forge::Ref<Forge::Scene> m_Scene;
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

	std::map<uint64_t, Motion> m_Motions;
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

	// Cells that should exist this frame, with the colour to draw them in.
	// Reused between frames so a full board does not allocate 200 map nodes
	// sixty times a second.
	std::map<uint64_t, glm::vec3> m_Occupied;

	std::string m_BlockMeshPath;

	bool m_FirstFrame = true;
	bool m_Initialized = false;
};

}  // namespace Tetris