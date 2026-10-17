#pragma once

#include "TetrisManager.h"
#include "TetrisComponents.h"

#include "Forge/Scene/Entity.h"

#include <cstdint>
#include <map>
#include <string>

namespace Tetris {

// Presents the game into an engine Scene.
//
// The rules live in TetrisManager and know nothing about entities; this is the
// only place the two meet. It mirrors the manager's state onto a handful of
// components (for tools and agents to read) and onto one mesh entity per
// occupied cell (for the renderer to draw).
// Shared material tuning so the board and the preview stay visually identical.
// The blocks are mirror-polished metal: full metallic and the lowest roughness
// the shader accepts, so the reflection probe shows the surroundings in them.
// The per-tetromino colour tints the reflection (F0 = albedo at metallic 1)
// and a small emissive term keeps piece identity readable.
constexpr float kBlockMetallic = 1.0f;
constexpr float kBlockRoughness = 0.04f;
constexpr float kBlockEmissiveIntensity = 0.06f;

// A shared brushed-metal albedo map. It modulates each tetromino colour instead
// of replacing it, keeping the piece identity readable while giving the blocks
// a reflective surface pattern.
constexpr char kBlockAlbedoTexture[] = "resources/textures/metal_blocks.bmp";

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

private:
	// Copy score, level and piece data onto the state components.
	void SyncStateComponents();

	// Create, move and destroy block entities so they match the board exactly.
	void SyncBlockEntities();

	// Entities created by Initialize, torn down by Shutdown.
	void DestroyEntity(Forge::Entity& entity);

	// Grid cell -> entity handle, so a cell keeps its entity between frames
	// instead of being destroyed and rebuilt every tick.
	static uint64_t CellKey(uint32_t col, uint32_t row);

	Forge::Ref<Forge::Scene> m_Scene;
	TetrisManager m_Manager;

	Forge::Entity m_GameEntity;     // TetrisGameComponent
	Forge::Entity m_BoardEntity;    // TetrisBoardComponent
	Forge::Entity m_PieceEntity;    // TetrisPieceComponent
	Forge::Entity m_DisplayEntity;  // TetrisDisplayComponent

	std::map<uint64_t, Forge::Entity> m_Blocks;

	// Cells that should exist this frame, with the colour to draw them in.
	// Reused between frames so a full board does not allocate 200 map nodes
	// sixty times a second.
	std::map<uint64_t, glm::vec3> m_Occupied;

	std::string m_BlockMeshPath;

	bool m_FirstFrame = true;
	bool m_Initialized = false;
};

}  // namespace Tetris