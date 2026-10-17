#include "fgpch.h"

#include "TetrisBoardView.h"

#include "Forge/Core/Log.h"
#include "Forge/Scene/Scene.h"

namespace Tetris {

namespace {

// The playfield: 10 columns by 20 rows of 1x1 blocks. It hangs below the world
// origin so the camera looks at the middle rows rather than at empty space, and
// is centred horizontally so the preview beside it has room.
constexpr float kColumns = 10.0f;
constexpr float kRows = 20.0f;
constexpr float kBlock = 1.0f;
constexpr float kOriginX = -5.0f;
constexpr float kOriginY = -10.0f;

// Blocks sit flush against the back wall of the well built by TetrisApp
// (interior spans z -1.2..0, blocks are 0.92 deep), so their side faces and
// the walls beside them read as a real pit from the angled camera.
constexpr float kBlockZ = -0.44f;

// Blocks are drawn slightly under a full cell so the grid reads as separate
// blocks rather than one merged slab.
constexpr float kBlockScale = 0.92f;

}  // namespace

BoardView::~BoardView()
{
	Shutdown();
}

uint64_t BoardView::CellKey(uint32_t col, uint32_t row)
{
	return (static_cast<uint64_t>(row) << 32) | static_cast<uint64_t>(col);
}

glm::vec3 BoardView::BoardOrigin()
{
	return glm::vec3(kOriginX, kOriginY, kBlockZ);
}

glm::vec3 BoardView::BoardCentre()
{
	return BoardOrigin() + glm::vec3(kColumns * kBlock * 0.5f, kRows * kBlock * 0.5f, 0.0f);
}

glm::vec3 BoardView::BoardSize()
{
	return glm::vec3(kColumns * kBlock, kRows * kBlock, 1.0f);
}

float BoardView::BlockSize()
{
	return kBlock;
}

glm::vec3 BoardView::CellToWorld(uint32_t col, uint32_t row)
{
	return BoardOrigin() + glm::vec3(
		(static_cast<float>(col) + 0.5f) * kBlock,
		(kRows - 1.0f - static_cast<float>(row) + 0.5f) * kBlock,
		0.0f);
}

bool BoardView::Initialize(Forge::Ref<Forge::Scene> scene, const std::string& blockMeshPath)
{
	if (!scene)
	{
		FG_ERROR("Tetris: cannot start without a scene");
		return false;
	}

	m_Scene = scene;
	m_BlockMeshPath = blockMeshPath;
	m_Manager.Initialize();

	// Four entities carry the game's state for anything inspecting the scene:
	// a tool, an agent, or a future UI. They hold no mesh, so they cost
	// nothing to render.
	m_GameEntity = m_Scene->CreateEntity("TetrisGame");
	auto& game = m_GameEntity.AddComponent<TetrisGameComponent>();
	game.IsRunning = true;
	game.BoardPosition = BoardOrigin();
	game.BoardWidth = kColumns;
	game.BoardHeight = kRows;
	game.BlockSize = kBlock;

	m_BoardEntity = m_Scene->CreateEntity("GameBoard");
	m_BoardEntity.AddComponent<TetrisBoardComponent>().Width = static_cast<uint32_t>(kColumns);

	m_PieceEntity = m_Scene->CreateEntity("CurrentPiece");
	m_PieceEntity.AddComponent<TetrisPieceComponent>();

	m_DisplayEntity = m_Scene->CreateEntity("GameDisplay");
	m_DisplayEntity.AddComponent<TetrisDisplayComponent>();

	m_Initialized = true;
	m_FirstFrame = true;
	FG_INFO("Tetris: playfield ready, {}x{}", static_cast<int>(kColumns), static_cast<int>(kRows));
	return true;
}

void BoardView::Update(float dt)
{
	if (!m_Initialized)
		return;

	// Start on the first update rather than in Initialize, so a caller that
	// configures the scene afterwards still sees the first piece spawn.
	if (m_FirstFrame)
	{
		m_FirstFrame = false;
		m_Manager.StartGame();
	}

	m_Manager.Update(dt);

	SyncStateComponents();
	SyncBlockEntities();
}

void BoardView::HandleAction(Action action, bool pressed)
{
	if (!m_Initialized)
		return;

	m_Manager.HandleAction(action, pressed);
}

void BoardView::Restart()
{
	if (!m_Initialized)
		return;

	m_Manager.StartGame();
	// The board is now empty, so the next SyncBlockEntities tears down every
	// block that was left standing.
	SyncStateComponents();
	SyncBlockEntities();
}

void BoardView::Shutdown()
{
	for (auto& [key, entity] : m_Blocks)
	{
		(void)key;
		DestroyEntity(entity);
	}
	m_Blocks.clear();
	m_Occupied.clear();

	DestroyEntity(m_PieceEntity);
	DestroyEntity(m_DisplayEntity);
	DestroyEntity(m_BoardEntity);
	DestroyEntity(m_GameEntity);

	m_Initialized = false;
}

void BoardView::DestroyEntity(Forge::Entity& entity)
{
	if (m_Scene && entity)
		m_Scene->DestroyEntity(entity);
	entity = Forge::Entity();
}

void BoardView::SyncStateComponents()
{
	const TetrisGameState& state = m_Manager.GetState();

	if (m_GameEntity)
	{
		auto& game = m_GameEntity.GetComponent<TetrisGameComponent>();
		game.IsRunning = state.CurrentState == TetrisGameState::State::Playing;
		game.IsPaused = state.CurrentState == TetrisGameState::State::Paused;
		game.IsGameOver = state.CurrentState == TetrisGameState::State::GameOver;
	}

	if (m_BoardEntity)
	{
		auto& board = m_BoardEntity.GetComponent<TetrisBoardComponent>();
		board.Width = state.Width;
		board.Height = state.Height;
		board.Board = state.Board;
	}

	if (m_PieceEntity)
	{
		auto& piece = m_PieceEntity.GetComponent<TetrisPieceComponent>();
		piece.Type = state.CurrentPiece.Type;
		piece.State = state.CurrentPiece.State;
		piece.GridPosition = state.CurrentPiece.Position;
		piece.Rotation = state.CurrentPiece.Rotation;
		piece.Color = TetrisManager::GetTetrominoDefinition(state.CurrentPiece.Type).color;
	}

	if (m_DisplayEntity)
	{
		auto& display = m_DisplayEntity.GetComponent<TetrisDisplayComponent>();
		display.Score = state.Score;
		display.Level = state.Level;
		display.LinesCleared = state.LinesCleared;
		display.Combo = state.Combo;
		display.NextPiece = state.NextPiece.Type;
		display.GameStateText = ToString(state.CurrentState);
	}
}

void BoardView::SyncBlockEntities()
{
	m_Occupied.clear();

	const TetrisGameState& state = m_Manager.GetState();

	// Locked cells, coloured by the piece that filled them.
	for (uint32_t row = 0; row < state.Height; ++row)
	{
		for (uint32_t col = 0; col < state.Width; ++col)
		{
			const uint32_t index = state.GetBoardIndex(glm::ivec2(static_cast<int32_t>(col), static_cast<int32_t>(row)));
			if (index >= state.Board.size() || state.Board[index] == 0)
				continue;

			const uint8_t type = static_cast<uint8_t>(state.Board[index] - 1);
			if (type >= static_cast<uint8_t>(TetrominoType::Count))
				continue;

			m_Occupied[CellKey(col, row)] = TetrisManager::GetTetrominoDefinition(static_cast<TetrominoType>(type)).color;
		}
	}

	// The falling piece, drawn from its rotated blocks.
	if (state.CurrentPiece.State == TetrisPieceState::Active)
	{
		for (const glm::ivec2& block : state.CurrentPiece.Blocks)
		{
			const glm::ivec2 cell = state.CurrentPiece.Position + block;
			if (!state.IsValidPosition(cell))
				continue;

			m_Occupied[CellKey(static_cast<uint32_t>(cell.x), static_cast<uint32_t>(cell.y))] =
				TetrisManager::GetTetrominoDefinition(state.CurrentPiece.Type).color;
		}
	}

	for (const auto& [key, color] : m_Occupied)
	{
		const auto cached = m_Blocks.find(key);
		if (cached == m_Blocks.end())
		{
			const uint32_t col = static_cast<uint32_t>(key & 0xFFFFFFFFu);
			const uint32_t row = static_cast<uint32_t>(key >> 32u);

			Forge::Entity entity = m_Scene->CreateEntity("TetBlock_" + std::to_string(col) + "_" + std::to_string(row));
			// CreateEntity already supplies a TransformComponent; setting it is
			// not adding a second one.
			auto& transform = entity.GetComponent<Forge::TransformComponent>();
			transform.Translation = CellToWorld(col, row);
			transform.Scale = glm::vec3(kBlockScale);

			auto& material = entity.AddComponent<Forge::PBRMaterialComponent>();
			material.Albedo = color;
			material.Metallic = kBlockMetallic;
			material.Roughness = kBlockRoughness;
			material.AO = 1.0f;
			material.AlbedoTexturePath = kBlockAlbedoTexture;
			material.Emissive = color;
			material.EmissiveIntensity = kBlockEmissiveIntensity;

			entity.AddComponent<Forge::MeshComponent>().ModelPath = m_BlockMeshPath;

			m_Blocks.emplace(key, entity);
		}
		else
		{
			auto& transform = cached->second.GetComponent<Forge::TransformComponent>();
			transform.Translation = CellToWorld(
				static_cast<uint32_t>(key & 0xFFFFFFFFu),
				static_cast<uint32_t>(key >> 32u));
			auto& cachedMaterial = cached->second.GetComponent<Forge::PBRMaterialComponent>();
			cachedMaterial.Albedo = color;
			cachedMaterial.Metallic = kBlockMetallic;
			cachedMaterial.Roughness = kBlockRoughness;
			cachedMaterial.AO = 1.0f;
			cachedMaterial.Emissive = color;
			cachedMaterial.EmissiveIntensity = kBlockEmissiveIntensity;
		}
	}

	// Anything the board no longer holds has to go. Skipping this leaves a
	// block standing at every cell the piece has ever visited, so the board
	// fills with ghosts of its own history and never appears to clear.
	for (auto it = m_Blocks.begin(); it != m_Blocks.end(); )
	{
		if (m_Occupied.count(it->first) != 0)
		{
			++it;
			continue;
		}

		Forge::Entity entity = it->second;
		DestroyEntity(entity);
		it = m_Blocks.erase(it);
	}
}

}  // namespace Tetris