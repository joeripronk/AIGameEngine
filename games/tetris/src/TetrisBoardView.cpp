#include "fgpch.h"

#include "TetrisBoardView.h"

#include "TetrisAnimation.h"

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

// A jump longer than this is a new piece appearing at the top, not a block
// moving: easing it from where the last one landed reads as the piece flying
// back up the well.
constexpr float kRespawnSnapCells = 2.5f;

// Seconds for the stack above a cleared row to fall into place.
constexpr float kStackDropSeconds = 0.11f;

// A lock that has nowhere to travel still settles, so a landing is never a
// pure pop.
constexpr float kSettleSeconds = 0.055f;

// The fastest a fall can be measured at. A soft-drop repeat interval is a
// hundredths of a second, and a glide shorter than this is a teleport again.
constexpr float kMinFallCadence = 0.02f;

// A frame never divides by itself: a stalled frame would report an infinite
// falling speed, and a floor is what keeps the shake inside the playfield.
constexpr float kMinImpactFrame = 1.0f / 120.0f;

// The fastest the drop interval is allowed to mean anything.
constexpr float kMinFallInterval = 0.05f;

// True when the piece's rotated blocks all sit on empty, in-bounds cells of
// `board`. The same test the rules make, against a board the rules have
// already moved on from.
bool FitsOnBoard(const std::vector<uint8_t>& board, uint32_t width, uint32_t height,
                 glm::ivec2 pos, const glm::ivec2 blocks[4])
{
	for (int i = 0; i < 4; ++i)
	{
		const glm::ivec2 cell = pos + blocks[i];
		if (cell.x < 0 || cell.x >= static_cast<int32_t>(width) ||
			cell.y < 0 || cell.y >= static_cast<int32_t>(height))
			return false;

		const uint32_t index = static_cast<uint32_t>(cell.y) * width + static_cast<uint32_t>(cell.x);
		if (index < board.size() && board[index] != 0)
			return false;
	}
	return true;
}

// Where a piece of `type` spawns: centred over the columns, on the top row.
// A lock is the only thing that puts a piece back there.
glm::ivec2 SpawnPosition(TetrominoType type, uint32_t width)
{
	const TetrominoDefinition& def = TetrisGameState::GetDefinition(type);
	int32_t minX = def.shape[0].x, maxX = def.shape[0].x;
	for (int i = 1; i < 4; ++i)
	{
		minX = std::min(minX, def.shape[i].x);
		maxX = std::max(maxX, def.shape[i].x);
	}
	return glm::ivec2((static_cast<int32_t>(width) - (maxX - minX + 1)) / 2, 0);
}

// The rows a clear removes, as the view has to see them: the board it was
// given, with the piece that locked written into it, full from edge to edge.
// The board is cleared in the same step that writes the piece, so nothing
// outside this snapshot records that a row was ever full.
std::vector<uint32_t> ClearedRows(const std::vector<uint8_t>& board, const TetrisPiece& piece,
                                  uint32_t width, uint32_t height)
{
	std::vector<uint8_t> filled = board;
	if (piece.State == TetrisPieceState::Active)
	{
		for (const glm::ivec2& block : piece.Blocks)
		{
			const glm::ivec2 cell = piece.Position + block;
			if (cell.x < 0 || cell.x >= static_cast<int32_t>(width) ||
				cell.y < 0 || cell.y >= static_cast<int32_t>(height))
				continue;

			const uint32_t index = static_cast<uint32_t>(cell.y) * width + static_cast<uint32_t>(cell.x);
			if (index < filled.size())
				filled[index] = static_cast<uint8_t>(piece.Type) + 1;
		}
	}

	std::vector<uint32_t> rows;
	for (uint32_t row = 0; row < height; ++row)
	{
		bool full = true;
		for (uint32_t col = 0; col < width; ++col)
		{
			const uint32_t index = row * width + col;
			if (index >= filled.size() || filled[index] == 0)
			{
				full = false;
				break;
			}
		}
		if (full)
			rows.push_back(row);
	}
	return rows;
}

}  // namespace

BoardView::~BoardView()
{
	Shutdown();
}

uint64_t BoardView::CellKey(uint32_t col, uint32_t row)
{
	return (static_cast<uint64_t>(row) << 32) | static_cast<uint64_t>(col);
}

uint64_t BoardView::SlotKey(uint32_t slot)
{
	return kSlotKeyBase | static_cast<uint64_t>(slot);
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

void BoardView::PreviewLocalOffsets(TetrominoType type, glm::vec3 outOffsets[4])
{
	glm::ivec2 blocks[4];
	GetRotatedBlocks(type, 0, blocks);

	// The shape's own bounding box, so centring is the shape's centre - not a
	// fixed corner. Anchoring every shape at the same corner put the 2-wide O
	// and 3-wide T, J, L, S, Z off to one side of the tray and let the 4-wide
	// I hang past its edge.
	int32_t minX = blocks[0].x, maxX = blocks[0].x;
	int32_t minY = blocks[0].y, maxY = blocks[0].y;
	for (int i = 1; i < 4; ++i)
	{
		minX = std::min(minX, blocks[i].x);
		maxX = std::max(maxX, blocks[i].x);
		minY = std::min(minY, blocks[i].y);
		maxY = std::max(maxY, blocks[i].y);
	}
	const float width = static_cast<float>(maxX - minX + 1);
	const float height = static_cast<float>(maxY - minY + 1);

	for (int i = 0; i < 4; ++i)
	{
		const float col = static_cast<float>(blocks[i].x - minX);
		// The playfield draws row 0 at the top by flipping Y; the preview has
		// to flip it the same way, or a T's nub appears above its bar and the
		// preview shows a piece that is not the one about to spawn.
		const float row = static_cast<float>(maxY - minY - (blocks[i].y - minY));

		// Cell centres measured from the bounding box's own centre, which is
		// the tray's centre: a 4-wide I spans +-1.5, a 2-wide O spans +-0.5.
		outOffsets[i] = glm::vec3(
			(col - (width - 1.0f) * 0.5f) * kBlock,
			(row - (height - 1.0f) * 0.5f) * kBlock,
			0.46f);
	}
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

	// The rules advance in one step. Everything after it is what that step
	// looks like: the snapshot is the state as of the *previous* Update, which
	// is the only way the view can see a hard drop at all - the input layer
	// drives the manager directly, so the lock happens between two Updates.
	m_Manager.Update(dt);

	// The rate the falling piece is moving at, measured between its downward
	// steps. Soft drop falls faster than gravity, so a glide pinned to the drop
	// interval would lag the key; the interval is the ceiling, not the value.
	{
		const TetrisGameState& advanced = m_Manager.GetState();
		if (m_Snapshot.Valid && advanced.CurrentPiece.Position.y > m_Snapshot.Piece.Position.y)
		{
			const float ceiling = std::max(advanced.PieceDropInterval, kMinFallCadence);
			m_FallCadence = std::min(std::max(m_TimeSinceFall, kMinFallCadence), ceiling);
			m_TimeSinceFall = 0.0f;
		}
		else
		{
			m_TimeSinceFall += dt;
		}
	}

	SyncStateComponents();
	DetectImpact(dt);
	SyncBoardEntities();
	SyncPieceEntities();
	AdvanceMotions(dt);
	TakeSnapshot();
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

	// A restart is not a landing. Nothing animates from the old board to the
	// new one, and the snapshot describing the old board is now a lie.
	DestroyPieceBlocks();
	m_Motions.clear();
	m_Snapshot = Snapshot{};
	m_LastImpact = Impact{};
	m_LastClearedRows.clear();
	m_FallCadence = 0.0f;
	m_TimeSinceFall = 0.0f;

	SyncStateComponents();
	SyncBoardEntities();
	SyncPieceEntities();
	TakeSnapshot();
}

void BoardView::Shutdown()
{
	DestroyPieceBlocks();
	m_Motions.clear();

	for (auto& [key, entity] : m_Blocks)
	{
		(void)key;
		DestroyEntity(entity);
	}
	m_Blocks.clear();
	m_Occupied.clear();

	m_Snapshot = Snapshot{};
	m_LastImpact = Impact{};
	m_LastClearedRows.clear();
	m_FallCadence = 0.0f;
	m_TimeSinceFall = 0.0f;

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

void BoardView::DestroyPieceBlocks()
{
	for (uint32_t slot = 0; slot < 4; ++slot)
	{
		DestroyEntity(m_PieceBlocks[slot]);
		m_Motions.erase(SlotKey(slot));
	}
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

void BoardView::SyncBoardEntities()
{
	m_Occupied.clear();

	const TetrisGameState& state = m_Manager.GetState();
	const bool locked = m_LastImpact.Occurred;

	// Rows shift down by the number of cleared rows below them, so a cell key
	// that is occupied now was written above it before the clear. Surviving
	// rows fill the board from the first row below the lowest clear.
	std::map<uint32_t, uint32_t> sourceRow;
	if (locked && !m_LastClearedRows.empty() && m_Snapshot.Valid)
	{
		std::vector<bool> cleared(state.Height, false);
		for (uint32_t row : m_LastClearedRows)
			if (row < state.Height)
				cleared[row] = true;

		uint32_t lowest = state.Height;
		for (uint32_t row = 0; row < state.Height; ++row)
		{
			if (!cleared[row])
			{
				lowest = row;
				break;
			}
		}

		uint32_t start = 0;
		for (uint32_t row = lowest + 1; row < state.Height; ++row)
			if (cleared[row])
				++start;

		uint32_t next = start;
		for (uint32_t row = 0; row < state.Height; ++row)
		{
			if (cleared[row])
				continue;
			if (next < state.Height)
				sourceRow[next++] = row;
		}
	}

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

			const auto pieceType = static_cast<TetrominoType>(type);
			const glm::vec3 color = TetrisManager::GetTetrominoDefinition(pieceType).color;
			const char* texture = GetBlockTexture(pieceType);
			const uint64_t key = CellKey(col, row);
			m_Occupied[key] = color;

			const glm::vec3 target = CellToWorld(col, row);
			const auto cached = m_Blocks.find(key);

			if (cached == m_Blocks.end())
			{
				// A cell the board did not hold before. On a landing it is one
				// of the locking piece's blocks, and it has to travel from
				// where that block was drawn - not appear at its final cell,
				// which is the instant Space used to be.
				glm::vec3 from = target;
				float duration = 0.0f;
				if (locked && m_Snapshot.Valid)
				{
					// Matched against the origin the piece locked at: the
					// landing cells are its blocks measured from there, while
					// the positions it is drawn at in the snapshot are where
					// it was when the fall began.
					for (uint32_t slot = 0; slot < 4; ++slot)
					{
						const glm::ivec2 cell = m_LastImpact.Landing + m_Snapshot.Piece.Blocks[slot];
						if (cell.x != static_cast<int32_t>(col) || cell.y != static_cast<int32_t>(row))
							continue;
						from = m_Snapshot.SlotPosition[slot];
						duration = StrikeDuration(m_LastImpact.Rows);
						break;
					}
					if (duration <= 0.0f)
						duration = kSettleSeconds;
				}

				Forge::Entity entity = m_Scene->CreateEntity("TetBlock_" + std::to_string(col) + "_" + std::to_string(row));
				// CreateEntity already supplies a TransformComponent; setting it is
				// not adding a second one.
				auto& transform = entity.GetComponent<Forge::TransformComponent>();
				transform.Translation = from;
				transform.Scale = glm::vec3(kBlockScale);

				auto& material = entity.AddComponent<Forge::PBRMaterialComponent>();
				material.Albedo = kBlockAlbedo;
				material.Metallic = kBlockMetallic;
				material.Roughness = kBlockRoughness;
				material.AO = 1.0f;
				material.AlbedoTexturePath = texture;
				material.Emissive = color;
				material.EmissiveIntensity = kBlockEmissiveIntensity;

				entity.AddComponent<Forge::MeshComponent>().ModelPath = m_BlockMeshPath;

				m_Blocks.emplace(key, entity);
				if (duration > 0.0f)
					m_Motions[key] = Motion{ from, target, 0.0f, duration, Curve::Strike };
				continue;
			}

			// A line clear shifts the rows above it down, so a cell can end up
			// holding a *different* piece than it did. The texture is rewritten
			// for the same reason the colour is: this block keeps its entity, so
			// anything not reassigned here is whatever the cell used to be. That
			// gap was invisible while every block shared one map.
			auto& cachedMaterial = cached->second.GetComponent<Forge::PBRMaterialComponent>();
			cachedMaterial.Albedo = kBlockAlbedo;
			cachedMaterial.Metallic = kBlockMetallic;
			cachedMaterial.Roughness = kBlockRoughness;
			cachedMaterial.AO = 1.0f;
			cachedMaterial.AlbedoTexturePath = texture;
			cachedMaterial.Emissive = color;
			cachedMaterial.EmissiveIntensity = kBlockEmissiveIntensity;

			if (m_Motions.count(key) != 0)
				continue;  // mid-flight: AdvanceMotions owns the transform

			// The block standing here now was written above it by the clear.
			const auto source = sourceRow.find(row);
			if (source != sourceRow.end() && source->second != row && m_Snapshot.Valid)
			{
				const uint32_t sourceIndex = source->second * state.Width + col;
				if (sourceIndex < m_Snapshot.Board.size() &&
					m_Snapshot.Board[sourceIndex] == state.Board[index])
				{
					m_Motions[key] = Motion{ CellToWorld(col, source->second), target, 0.0f, kStackDropSeconds, Curve::Slide };
					continue;
				}
			}

			cached->second.GetComponent<Forge::TransformComponent>().Translation = target;
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
		m_Motions.erase(it->first);
		it = m_Blocks.erase(it);
	}
}

void BoardView::SyncPieceEntities()
{
	const TetrisGameState& state = m_Manager.GetState();
	const bool active =
		state.CurrentPiece.State == TetrisPieceState::Active &&
		state.CurrentState == TetrisGameState::State::Playing;

	if (!active)
	{
		DestroyPieceBlocks();
		return;
	}

	const auto pieceType = state.CurrentPiece.Type;
	const glm::vec3 color = TetrisManager::GetTetrominoDefinition(pieceType).color;
	const char* texture = GetBlockTexture(pieceType);

	// A downward step is the piece falling, not the player moving it: a fall is
	// drawn at the rate the rules are falling, so the block glides cell after
	// cell instead of hopping and pausing once per row.
	const bool falling =
		m_Snapshot.Valid &&
		m_FallCadence > 0.0f &&
		state.CurrentPiece.Position.y > m_Snapshot.Piece.Position.y;

	for (uint32_t slot = 0; slot < 4; ++slot)
	{
		const glm::ivec2 cell = state.CurrentPiece.Position + state.CurrentPiece.Blocks[slot];
		if (!state.IsValidPosition(cell))
		{
			DestroyEntity(m_PieceBlocks[slot]);
			m_Motions.erase(SlotKey(slot));
			continue;
		}

		const glm::vec3 target = CellToWorld(static_cast<uint32_t>(cell.x), static_cast<uint32_t>(cell.y));

		if (!m_PieceBlocks[slot])
		{
			Forge::Entity entity = m_Scene->CreateEntity("TetBlock_Piece" + std::to_string(slot));
			auto& transform = entity.GetComponent<Forge::TransformComponent>();
			transform.Translation = target;
			transform.Scale = glm::vec3(kBlockScale);

			auto& material = entity.AddComponent<Forge::PBRMaterialComponent>();
			material.Albedo = kBlockAlbedo;
			material.Metallic = kBlockMetallic;
			material.Roughness = kBlockRoughness;
			material.AO = 1.0f;
			material.AlbedoTexturePath = texture;
			material.Emissive = color;
			material.EmissiveIntensity = kBlockEmissiveIntensity;

			entity.AddComponent<Forge::MeshComponent>().ModelPath = m_BlockMeshPath;

			m_PieceBlocks[slot] = entity;
			m_SlotPosition[slot] = target;
			continue;
		}

		// The four piece blocks are reused for every piece, so the surface has
		// to follow the type: without this a block keeps the previous piece's
		// tile right up until it locks and the board copies it.
		auto& cachedMaterial = m_PieceBlocks[slot].GetComponent<Forge::PBRMaterialComponent>();
		cachedMaterial.Albedo = kBlockAlbedo;
		cachedMaterial.Metallic = kBlockMetallic;
		cachedMaterial.Roughness = kBlockRoughness;
		cachedMaterial.AO = 1.0f;
		cachedMaterial.AlbedoTexturePath = texture;
		cachedMaterial.Emissive = color;
		cachedMaterial.EmissiveIntensity = kBlockEmissiveIntensity;

		const float distance = glm::length(target - m_SlotPosition[slot]);
		const auto motion = m_Motions.find(SlotKey(slot));

		if (distance > kRespawnSnapCells)
		{
			// A new piece appears at the top of the well. Easing a block
			// fifteen rows upward reads as the piece flying back up, so a
			// respawn arrives where it is and stays.
			m_Motions.erase(SlotKey(slot));
			m_PieceBlocks[slot].GetComponent<Forge::TransformComponent>().Translation = target;
			m_SlotPosition[slot] = target;
		}
		else if (distance > 1e-4f)
		{
			const bool inFlight = motion != m_Motions.end();
			if (!inFlight || motion->second.To != target)
			{
				const float duration = falling ? m_FallCadence : SlideDuration(distance);
				const Curve curve = falling ? Curve::Glide : (distance > 1.5f ? Curve::Strike : Curve::Slide);
				m_Motions[SlotKey(slot)] = Motion{ m_SlotPosition[slot], target, 0.0f, duration, curve };
			}
		}
	}
}

void BoardView::AdvanceMotions(float dt)
{
	for (auto it = m_Motions.begin(); it != m_Motions.end(); )
	{
		Motion& motion = it->second;
		motion.Elapsed += dt;

		const float t = motion.Duration > 0.0f ? Clamp01(motion.Elapsed / motion.Duration) : 1.0f;
		const float eased = motion.CurveType == Curve::Strike
			? StrikeEase(t)
			: (motion.CurveType == Curve::Glide ? GlideRate(t) : EaseInOut(t));
		const glm::vec3 position = glm::mix(motion.From, motion.To, eased);

		Forge::Entity* entity = nullptr;
		if ((it->first & kSlotKeyBase) != 0)
		{
			const uint32_t slot = static_cast<uint32_t>(it->first & 0xFFFFFFFFu);
			if (slot < 4)
			{
				entity = &m_PieceBlocks[slot];
				m_SlotPosition[slot] = position;
			}
		}
		else
		{
			const auto found = m_Blocks.find(it->first);
			if (found != m_Blocks.end())
				entity = &found->second;
		}

		if (entity && *entity)
			entity->GetComponent<Forge::TransformComponent>().Translation = position;

		if (t >= 1.0f)
			it = m_Motions.erase(it);
		else
			++it;
	}
}

void BoardView::DetectImpact(float dt)
{
	m_LastImpact = Impact{};
	m_LastClearedRows.clear();

	if (!m_Snapshot.Valid)
		return;
	if (m_Snapshot.Piece.State != TetrisPieceState::Active)
		return;

	const TetrisGameState& state = m_Manager.GetState();
	if (state.CurrentState != TetrisGameState::State::Playing)
		return;

	const TetrisPiece& previous = m_Snapshot.Piece;

	// A lock is the only thing that writes cells into the board, and the only
	// thing that puts a piece back on its spawn row at rotation 0. Comparing
	// the piece is not enough: a piece that locks where it spawned is
	// indistinguishable from the piece that just spawned there.
	if (state.Board == m_Snapshot.Board)
		return;
	if (state.CurrentPiece.Position != SpawnPosition(state.CurrentPiece.Type, state.Width))
		return;
	// A spawned piece is the one the rules just created: rotation 0, on its
	// spawn row. Rotation is the only action that leaves a piece on its spawn
	// row, and it writes nothing into the board.
	if (state.CurrentPiece.Rotation != 0)
		return;

	// How far it fell, measured against the board it locked onto.
	glm::ivec2 landing = previous.Position;
	while (FitsOnBoard(m_Snapshot.Board, state.Width, state.Height, landing + glm::ivec2(0, 1), previous.Blocks))
		++landing.y;
	const float rows = static_cast<float>(landing.y - previous.Position.y);

	m_LastClearedRows = ClearedRows(m_Snapshot.Board, previous, state.Width, state.Height);

	// A lock writes the piece's blocks, at the position it came to rest in,
	// into the board - or the row it filled is gone in the same step. A move
	// and a rotation write nothing, and the cells below the piece are empty
	// for both, so measuring the fall is what separates them.
	bool landed = true;
	for (const glm::ivec2& block : previous.Blocks)
	{
		const glm::ivec2 cell = landing + block;
		if (!state.IsValidPosition(cell))
		{
			landed = false;
			break;
		}
		const uint32_t index = state.GetBoardIndex(cell);
		if (index >= state.Board.size() || state.Board[index] == 0)
		{
			landed = false;
			break;
		}
	}
	if (!landed && m_LastClearedRows.empty())
		return;

	// The speed the blocks were moving at when they stopped. A hard drop covers
	// the distance in the frame it locks; a gravity lock does not move in that
	// frame at all, and was falling at the drop rate.
	const float frame = std::max(dt, kMinImpactFrame);
	const float speed = rows > 0.0f
		? rows / frame
		: 1.0f / std::max(state.PieceDropInterval, kMinFallInterval);

	m_LastImpact = Impact{ true, rows, speed, landing };
}

void BoardView::TakeSnapshot()
{
	const TetrisGameState& state = m_Manager.GetState();
	m_Snapshot.Board = state.Board;
	m_Snapshot.Piece = state.CurrentPiece;
	for (uint32_t slot = 0; slot < 4; ++slot)
		m_Snapshot.SlotPosition[slot] = m_SlotPosition[slot];
	m_Snapshot.Valid = true;
}

}  // namespace Tetris