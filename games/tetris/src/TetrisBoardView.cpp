#include "fgpch.h"

#include "TetrisBoardView.h"

#include "TetrisAnimation.h"

#include <algorithm>

#include "Forge/Core/Log.h"
#include "Forge/Scene/Scene.h"

namespace Tetris {

namespace {

// The playfield: 10 columns by 20 rows. Each cell is kBlock world units on a
// side, so the board's footprint (and the camera framing, decor and preview
// that derive from BoardSize) scales with it: a bigger block fills more of the
// viewport. The board hangs below the world origin so the camera looks at the
// middle rows rather than at empty space, and is centred horizontally so the
// preview beside it has room.
constexpr float kColumns = 10.0f;
constexpr float kRows = 20.0f;
constexpr float kBlock = 1.2f;
constexpr float kOriginX = -5.0f;
constexpr float kOriginY = -10.0f;

// Blocks sit flush against the back wall of the well built by TetrisApp
// (interior spans z -1.2..0, blocks are 0.92 deep), so their side faces and
// the walls beside them read as a real pit from the angled camera.
constexpr float kBlockZ = -0.44f;

// Blocks are drawn slightly under a full cell so the grid reads as separate
// blocks rather than one merged slab.
constexpr float kBlockScale = 0.92f;

// Depth of a block along z, in world units. The mesh is a unit cube scaled by
// kBlockScale on every axis, so at kBlockScale the block is 0.92 deep and
// protrudes most of the way out of the playfield plane. The main scene pass
// occludes by a per-object centroid sort (no per-pixel depth test), which is
// only correct for geometry that does not overlap on screen: a cube this deep
// leans toward the angled camera and covers the row above it, and the centroid
// behind it decides the draw order, so a lower block paints over the row it
// belongs behind. Keeping the block a shallow slab - deep enough for the top
// faces and bevels to catch the key light and read as solid, shallow enough
// that neighbours tile the plane without overlapping - is what makes the sort
// agree with what the eye expects.
constexpr float kBlockDepth = 0.45f;

// A jump longer than this is a new piece appearing at the top, not a block
// moving: easing it from where the last one landed reads as the piece flying
// back up the well.
constexpr float kRespawnSnapCells = 2.5f;

// Seconds a cleared row's blocks take to shrink away. The stack above the row
// waits this long before it starts falling, so the row goes out first.
constexpr float kLineClearSeconds = 0.25f;

// Seconds a melted block takes to burn out. A touch longer than a line clear:
// a single block burning has no stack waiting on it, so it can take its time.
constexpr float kMeltSeconds = 0.35f;

// Extra emissive intensity a melting block reaches at the peak of its burn.
constexpr float kFireMeltGlow = 1.4f;

// Extra emissive intensity the falling fire block flashes up to at the peak of
// its pulse.
constexpr float kFireFlashGlow = 0.9f;

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

// The name a block answers to: the cell it sits in. A tool reading the scene by
// name, and the tests that say what the player sees, both find a block by where
// it is, so a block that moves has to carry this name with it.
std::string BlockName(uint32_t col, uint32_t row)
{
	return "TetBlock_" + std::to_string(col) + "_" + std::to_string(row);
}

// The name of a block leaving a cleared row. A block animating out of a row and
// the block that dropped into that same row share the row's name, and both are
// there at the same time, so the one that is vanishing has to be findable apart
// from the one that is arriving.
std::string ClearingBlockName(uint32_t col, uint32_t row)
{
	return "TetBlockClear_" + std::to_string(col) + "_" + std::to_string(row);
}

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
// given, with the piece that locked written into it at the origin it came to
// rest in, packed the way a hard drop packs it, full from edge to edge. The
// board is cleared in the same step that writes the piece, so nothing outside
// this snapshot records that a row was ever full.
//
// The origin is the position the piece locked at, not the position the snapshot
// has it in: a hard drop moves the piece all the way down and locks it there,
// so the snapshot has it at the top of the well. Writing the piece at its
// snapshot row asks whether the spawn row is full, which it never is, and the
// line that was just completed goes unrecorded - no clear animation, and no
// impact, because a lock that cleared a row and wrote nothing at its landing
// cells reports neither.
//
// Compaction has to be applied here for the same reason: the rows a hard drop
// completes are the rows full *after* the stack packed, and a row with a hole
// in it is not full before the block above it falls into the hole. Measuring
// fullness on the unpacked board is what makes a compaction clear invisible.
std::vector<uint32_t> ClearedRows(const std::vector<uint8_t>& board, const TetrisPiece& piece,
                                   glm::ivec2 landing, uint32_t width, uint32_t height,
                                   const std::vector<CompactionMove>& moves)
{
	std::vector<uint8_t> filled = board;
	if (piece.State == TetrisPieceState::Active)
	{
		for (const glm::ivec2& block : piece.Blocks)
		{
			const glm::ivec2 cell = landing + block;
			if (cell.x < 0 || cell.x >= static_cast<int32_t>(width) ||
				cell.y < 0 || cell.y >= static_cast<int32_t>(height))
				continue;

			const uint32_t index = static_cast<uint32_t>(cell.y) * width + static_cast<uint32_t>(cell.x);
			if (index < filled.size())
				filled[index] = static_cast<uint8_t>(piece.Type) + 1;
		}
	}

	// The rules compact in recorded order, which packs each column from its
	// floor upwards, so replaying it in the same order lands every block on the
	// cell the rules put it in.
	for (const CompactionMove& move : moves)
	{
		const uint32_t from = move.FromRow * width + move.Column;
		const uint32_t to = move.ToRow * width + move.Column;
		if (from >= filled.size() || to >= filled.size())
			continue;

		filled[to] = filled[from];
		filled[from] = 0;
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

// The row a block that locked into `row` ends up in once the stack has packed.
// A hard drop slides it down into the gap beneath it, so the cell it locked
// into is not always the cell it comes to rest in - and a landing check that
// only looks at the cell it locked into reports a lock that compacted as a
// lock that never happened.
int32_t CompactedRow(const std::vector<CompactionMove>& moves, int32_t col, int32_t row)
{
	for (const CompactionMove& move : moves)
	{
		if (static_cast<int32_t>(move.Column) == col && static_cast<int32_t>(move.FromRow) == row)
			return static_cast<int32_t>(move.ToRow);
	}
	return row;
}

// True when `block` ended up standing in the current board, following the
// compaction slide the rules applied to it on the way in.
bool LandedOnBoard(const std::vector<uint8_t>& board, uint32_t width, uint32_t height,
                   int32_t col, int32_t row, const std::vector<CompactionMove>& moves)
{
	if (col < 0 || col >= static_cast<int32_t>(width) ||
		row < 0 || row >= static_cast<int32_t>(height))
		return false;

	const uint32_t index = static_cast<uint32_t>(row) * width + static_cast<uint32_t>(col);
	if (index < board.size() && board[index] != 0)
		return true;

	const int32_t resting = CompactedRow(moves, col, row);
	if (resting == row || resting < 0 || resting >= static_cast<int32_t>(height))
		return false;

	const uint32_t restingIndex = static_cast<uint32_t>(resting) * width + static_cast<uint32_t>(col);
	return restingIndex < board.size() && board[restingIndex] != 0;
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

BoardView::PreviewTray BoardView::GetPreviewTray(bool threeDimensional)
{
	// The tray's cells: four by four, two cells clear of the board.
	constexpr float kPreviewColumns = 4.0f;
	constexpr float kPreviewRows = 4.0f;
	constexpr float kPreviewGap = 2.0f;

	const glm::vec3 centre = BoardCentre();
	const glm::vec3 size = BoardSize();
	const float block = BlockSize();

	PreviewTray tray;
	tray.Cell = block;
	tray.HalfWidth = (kPreviewColumns + 0.5f) * block * 0.5f;
	tray.HalfHeight = (kPreviewRows + 0.5f) * block * 0.5f;

	// Beside the playfield, near its top: the piece is read at a glance, so
	// it sits where the eye goes next - off to the side of the board, not on
	// top of it.
	tray.Centre = centre + glm::vec3(
		size.x * 0.5f + block * (kPreviewGap + kPreviewColumns * 0.5f),
		size.y * 0.5f - 2.0f * block,
		// The flat view keeps the preview in the board's own plane. The
		// angled well holds it in front of a tray of its own, so it sits
		// further back than the board's blocks.
		threeDimensional ? -0.3f : 0.0f);

	return tray;
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

bool BoardView::Initialize(Forge::Scene* scene, const std::string& blockMeshPath)
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
	AdvanceClearAnimations(dt);
	TakeSnapshot();

	// The fire block's flash runs on a continuous clock.
	m_FireFlash += dt;
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
	for (auto& [key, cb] : m_ClearingBlocks)
	{
		(void)key;
		Forge::Entity entity = cb.Entity;
		DestroyEntity(entity);
	}
	m_ClearingBlocks.clear();
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

	for (auto& [key, cb] : m_ClearingBlocks)
	{
		(void)key;
		Forge::Entity entity = cb.Entity;
		DestroyEntity(entity);
	}
	m_ClearingBlocks.clear();

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

void BoardView::RenameEntity(Forge::Entity entity, const std::string& name)
{
	if (!entity)
		return;

	entity.GetComponent<Forge::TagComponent>().Tag = name;
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
		piece.IsFire = state.CurrentPiece.IsFire;
		piece.Color = state.CurrentPiece.IsFire
			? TetrisManager::kFireColor
			: TetrisManager::GetTetrominoDefinition(state.CurrentPiece.Type).color;
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
	std::vector<bool> cleared(state.Height, false);
	std::map<uint32_t, uint32_t> sourceRow;
	const bool clearing = locked && !m_LastClearedRows.empty() && m_Snapshot.Valid;
	if (clearing)
	{
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

	// A hard drop packed the stack on the way in, and a packed block moves to a
	// key the cache has never heard of for the same reason a cleared row's block
	// does: it keeps its entity and its name, so it slides into the gap it fell
	// into instead of being destroyed at the top of the gap and built at the
	// bottom of it. Relabelling happens before the clear takes any row out,
	// because the rules compacted first - the rows that clear are the rows the
	// pack filled.
	//
	// The slide waits for the landing strike to finish: the block the player
	// just dropped has to come to rest before the stack beneath it closes, and a
	// strike and a slide in the same frame read as one block passing through
	// another.
	const std::vector<CompactionMove>& moves = state.CompactedCells;
	const float compactDelay = locked
		? StrikeDuration(m_LastImpact.Rows + m_LastImpact.Compact)
		: 0.0f;
	float compactSlide = 0.0f;
	for (const CompactionMove& move : moves)
	{
		if (move.Column >= state.Width || move.FromRow >= state.Height || move.ToRow >= state.Height)
			continue;

		const uint64_t from = CellKey(move.Column, move.FromRow);
		const auto block = m_Blocks.find(from);
		if (block == m_Blocks.end())
			continue;  // nothing was drawn there, so nothing slid

		const uint64_t to = CellKey(move.Column, move.ToRow);
		Forge::Entity entity = block->second;

		m_Motions.erase(from);
		m_Blocks.erase(block);
		m_Blocks[to] = entity;
		RenameEntity(entity, BlockName(move.Column, move.ToRow));

		const float distance = static_cast<float>(move.ToRow - move.FromRow);
		const float duration = SlideDuration(distance);
		if (duration > compactSlide)
			compactSlide = duration;
		m_Motions[to] = Motion{ CellToWorld(move.Column, move.FromRow),
			CellToWorld(move.Column, move.ToRow), 0.0f, duration, Curve::Slide, compactDelay };
	}

	// Everything the clear animates waits for the pack to finish: the rows a
	// hard drop completes are the rows the pack filled, so a row flashing out
	// while the block that filled it is still travelling is the reward
	// disappearing before the player sees it land. With nothing packed there is
	// nothing to wait for, and a clear that has no compaction in front of it
	// must start when the piece lands, as it always has.
	const float clearStart = moves.empty() ? 0.0f : compactDelay + compactSlide;

	// A cleared row's blocks leave the cache by row, not by occupancy. They are
	// the cells the rows above are about to fall into, so taking them out first
	// is what keeps those keys free for the drop - and moving them out by row
	// is what animates every block of the row, not the ones whose keys happen
	// to be left unoccupied by the shift.
	if (clearing)
	{
		for (auto it = m_Blocks.begin(); it != m_Blocks.end(); )
		{
			const uint32_t row = static_cast<uint32_t>(it->first >> 32);
			const uint32_t col = static_cast<uint32_t>(it->first & 0xFFFFFFFFu);
			if (row >= cleared.size() || !cleared[row])
			{
				++it;
				continue;
			}

			const glm::vec3 cell = CellToWorld(col, row);

			ClearingBlock cb;
			cb.Entity = it->second;
			cb.Color = it->second.GetComponent<Forge::PBRMaterialComponent>().Albedo;
			cb.From = cell;
			cb.To = cell;
			cb.TravelDuration = 0.0f;
			cb.Elapsed = 0.0f;
			cb.Duration = kLineClearSeconds;
			cb.Delay = clearStart;

			RenameEntity(it->second, ClearingBlockName(col, row));
			m_ClearingBlocks[it->first] = cb;
			m_Motions.erase(it->first);
			it = m_Blocks.erase(it);
		}
	}

	// Blocks are keyed by the cell they sit in, so a row that shifts down moves
	// every block in it to a key the cache has never heard of: the entity was
	// found under its old key, the new cell found nothing, and the stack above
	// a cleared row arrived in one frame. The motion that measures a block
	// against the row it was written in below can never run, because the block
	// it looks for is never there. Relabelling the keys - destination row by
	// destination row, from the bottom up, so a key is only ever written into
	// after its occupant has moved - is what lets a block keep its entity and
	// its name across the drop.
	if (clearing)
	{
		for (uint32_t row = state.Height; row-- > 0; )
		{
			const auto src = sourceRow.find(row);
			if (src == sourceRow.end() || src->second == row)
				continue;

			for (uint32_t col = 0; col < state.Width; ++col)
			{
				const auto block = m_Blocks.find(CellKey(col, src->second));
				if (block == m_Blocks.end())
					continue;

				const uint64_t dest = CellKey(col, row);
				Forge::Entity entity = block->second;

				// The block's motion in the cache is the one that was taking it
				// to the cell it is leaving, which the clear has just made a
				// different cell. Carrying it over hands the block to a curve
				// that ends at the old row, and the drop below cannot start
				// while a motion is in flight - so the key is dropped and the
				// block is handed to the drop from its source row.
				m_Motions.erase(block->first);

				m_Blocks.erase(block);
				m_Blocks[dest] = entity;
				RenameEntity(entity, BlockName(col, row));
			}
		}
	}

	// The blocks that completed the line are part of the line. They have no
	// board cell to be found in - the row they filled is removed in the same
	// step the piece locked - so neither the board nor the cache records that
	// they were ever there. Left out, the row's old blocks fade out and the
	// blocks the player just placed blink out in the frame the line clears:
	// precisely the instant the player is looking at.
	if (clearing)
	{
		const glm::vec3 pieceColor =
			TetrisManager::GetTetrominoDefinition(m_Snapshot.Piece.Type).color;

		for (uint32_t slot = 0; slot < 4; ++slot)
		{
			const glm::ivec2 landed = m_LastImpact.Landing + m_Snapshot.Piece.Blocks[slot];
			// The block that completed the row may have been packed down into
			// it by the same drop, so the row it filled is the row it came to
			// rest in, not the row it locked into.
			const int32_t restingRow = CompactedRow(moves, landed.x, landed.y);
			if (landed.x < 0 || landed.x >= static_cast<int32_t>(state.Width) ||
				restingRow < 0 || restingRow >= static_cast<int32_t>(state.Height))
				continue;

			const uint32_t row = static_cast<uint32_t>(restingRow);
			const uint32_t col = static_cast<uint32_t>(landed.x);
			if (row >= cleared.size() || !cleared[row])
				continue;

			const uint64_t key = CellKey(col, row);
			if (m_ClearingBlocks.count(key) != 0)
				continue;  // that cell already has a block leaving it

			const glm::vec3 from = m_Snapshot.SlotPosition[slot];
			const glm::vec3 target = CellToWorld(col, row);

			Forge::Entity entity = m_Scene->CreateEntity(ClearingBlockName(col, row));
			auto& transform = entity.GetComponent<Forge::TransformComponent>();
			transform.Translation = from;
			transform.Scale = glm::vec3(kBlockScale, kBlockScale, kBlockDepth);

			auto& material = entity.AddComponent<Forge::PBRMaterialComponent>();
			material.Albedo = pieceColor;
			material.Metallic = kBlockMetallic;
			material.Roughness = kBlockRoughness;
			material.AO = kBlockAO;
			material.Emissive = pieceColor;
			material.EmissiveIntensity = kBlockEmissiveIntensity;

			entity.AddComponent<Forge::MeshComponent>().ModelPath = m_BlockMeshPath;

			ClearingBlock cb;
			cb.Entity = entity;
			cb.Color = pieceColor;
			cb.From = from;
			cb.To = target;
			cb.TravelDuration = StrikeDuration(m_LastImpact.Rows + m_LastImpact.Compact);
			cb.Elapsed = 0.0f;
			cb.Duration = kLineClearSeconds;
			cb.Delay = clearStart;
			m_ClearingBlocks[key] = cb;
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
					// it was when the fall began. A hard drop packs the stack
					// as it locks, so the cell a block ends in is the cell it
					// slid down to, and the strike covers the fall and the
					// slide as one travel.
					for (uint32_t slot = 0; slot < 4; ++slot)
					{
						const glm::ivec2 cell = m_LastImpact.Landing + m_Snapshot.Piece.Blocks[slot];
						if (cell.x != static_cast<int32_t>(col) ||
							CompactedRow(moves, cell.x, cell.y) != static_cast<int32_t>(row))
							continue;
						from = m_Snapshot.SlotPosition[slot];
						duration = StrikeDuration(m_LastImpact.Rows + m_LastImpact.Compact);
						break;
					}
					if (duration <= 0.0f)
						duration = kSettleSeconds;
				}

				Forge::Entity entity = m_Scene->CreateEntity(BlockName(col, row));
				// CreateEntity already supplies a TransformComponent; setting it is
				// not adding a second one.
				auto& transform = entity.GetComponent<Forge::TransformComponent>();
				transform.Translation = from;
				transform.Scale = glm::vec3(kBlockScale, kBlockScale, kBlockDepth);

				auto& material = entity.AddComponent<Forge::PBRMaterialComponent>();
				material.Albedo = color;
				material.Metallic = kBlockMetallic;
				material.Roughness = kBlockRoughness;
				material.AO = kBlockAO;
				material.Emissive = color;
				material.EmissiveIntensity = kBlockEmissiveIntensity;

				entity.AddComponent<Forge::MeshComponent>().ModelPath = m_BlockMeshPath;

				m_Blocks.emplace(key, entity);
				if (duration > 0.0f)
					m_Motions[key] = Motion{ from, target, 0.0f, duration, Curve::Strike, 0.0f };
				continue;
			}

			// A line clear shifts the rows above it down, so a cell can end up
			// holding a *different* piece than it did. The texture is rewritten
			// for the same reason the colour is: this block keeps its entity, so
			// anything not reassigned here is whatever the cell used to be. That
			// gap was invisible while every block shared one map.
			auto& cachedMaterial = cached->second.GetComponent<Forge::PBRMaterialComponent>();
			cachedMaterial.Albedo = color;
			cachedMaterial.Metallic = kBlockMetallic;
			cachedMaterial.Roughness = kBlockRoughness;
			cachedMaterial.AO = kBlockAO;
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
					// The stack cannot start falling while the row it falls
					// into is still animating away: a block sliding through a
					// shrinking row is two animations in one cell, and the
					// clear reads as the stack moving through a fading wall.
					// The drop waits for the row to go, and a row that falls
					// two cells buys its time the way a block moving two cells
					// always has. A hard drop's pack runs first, so the row
					// itself starts that much later.
					m_Motions[key] = Motion{ CellToWorld(col, source->second), target, 0.0f,
						SlideDuration(static_cast<float>(row - source->second)), Curve::Slide,
						clearStart + kLineClearSeconds };
					continue;
				}
			}

			cached->second.GetComponent<Forge::TransformComponent>().Translation = target;
		}
	}

	// Blocks a fire block just melted animate out (burning toward fire) before
	// they are destroyed, so they leave the cache before the sweep below.
	SeedMeltAnimations();

	// Anything the board no longer holds has to go. Skipping this leaves a
	// block standing at every cell the piece has ever visited, so the board
	// fills with ghosts of its own history and never appears to clear. The
	// blocks of a cleared row left the cache by row at the start of this
	// function, so whatever is in the cache and not on the board now is a
	// block that is not there any more.
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

	// Each compaction is presented once, in the frame that made it. The list
	// lives until the next hard drop, so leaving it here would slide the same
	// blocks a second time - and the second slide would be measured against
	// rows the first one already moved.
	m_Manager.GetState().CompactedCells.clear();
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
	const glm::vec3 color = state.CurrentPiece.IsFire
		? TetrisManager::kFireColor
		: TetrisManager::GetTetrominoDefinition(pieceType).color;

	// A fire block flashes: its glow throbs on a fixed pulse. A normal piece
	// keeps the steady rim emissive.
	const float emissiveIntensity = state.CurrentPiece.IsFire
		? kBlockEmissiveIntensity + kFireFlashGlow * FireFlashPulse(m_FireFlash)
		: kBlockEmissiveIntensity;

	// A downward step is the piece falling, not the player moving it: a fall is
	// drawn at the rate the rules are falling, so the block glides cell after
	// cell instead of hopping and pausing once per row.
	const bool falling =
		m_Snapshot.Valid &&
		m_FallCadence > 0.0f &&
		state.CurrentPiece.Position.y > m_Snapshot.Piece.Position.y;

	for (uint32_t slot = 0; slot < 4; ++slot)
	{
		// A fire block that has burned away is no longer part of the piece.
		if (!state.CurrentPiece.BlockAlive[slot])
		{
			DestroyEntity(m_PieceBlocks[slot]);
			m_Motions.erase(SlotKey(slot));
			continue;
		}

		const glm::ivec2 cell = state.CurrentPiece.BlockPos[slot];
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
			transform.Scale = glm::vec3(kBlockScale, kBlockScale, kBlockDepth);

			auto& material = entity.AddComponent<Forge::PBRMaterialComponent>();
			material.Albedo = color;
			material.Metallic = kBlockMetallic;
			material.Roughness = kBlockRoughness;
			material.AO = kBlockAO;
			material.Emissive = color;
			material.EmissiveIntensity = emissiveIntensity;

			entity.AddComponent<Forge::MeshComponent>().ModelPath = m_BlockMeshPath;

			m_PieceBlocks[slot] = entity;
			m_SlotPosition[slot] = target;
			continue;
		}

		// The four piece blocks are reused for every piece, so the surface has
		// to follow the type: without this a block keeps the previous piece's
		// tile right up until it locks and the board copies it.
		auto& cachedMaterial = m_PieceBlocks[slot].GetComponent<Forge::PBRMaterialComponent>();
		cachedMaterial.Albedo = color;
		cachedMaterial.Metallic = kBlockMetallic;
		cachedMaterial.Roughness = kBlockRoughness;
		cachedMaterial.AO = kBlockAO;
		cachedMaterial.Emissive = color;
		cachedMaterial.EmissiveIntensity = emissiveIntensity;

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

		// A delayed motion leaves the block where it started: the stack above a
		// cleared row stands on the row it was drawn on last frame while the
		// row shrinks, so the wait is not a jump.
		const float local = motion.Elapsed - motion.Delay;
		const float t = local <= 0.0f
			? 0.0f
			: (motion.Duration > 0.0f ? Clamp01(local / motion.Duration) : 1.0f);
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

		if (motion.Elapsed >= motion.Delay + motion.Duration)
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
	// A fire block never lands - it burns - so it has no landing to measure.
	if (m_Snapshot.Piece.IsFire)
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

	// A hard drop packs the stack on the way in, and the rows it completes are
	// the rows full after the pack, not before it.
	const std::vector<CompactionMove>& moves = state.CompactedCells;
	m_LastClearedRows = ClearedRows(m_Snapshot.Board, previous, landing, state.Width, state.Height, moves);

	// How far the landing blocks slid as the stack packed. The strike that
	// carries a dropped block to its resting cell covers the fall *and* the
	// slide, so a compaction that buries a block deep is measured as the
	// distance it travelled, not as the cell it locked into.
	float compact = 0.0f;
	for (const glm::ivec2& block : previous.Blocks)
	{
		const int32_t resting =
			CompactedRow(moves, landing.x + block.x, landing.y + block.y);
		const float slide = static_cast<float>(resting - (landing.y + block.y));
		if (slide > compact)
			compact = slide;
	}

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
		if (!LandedOnBoard(state.Board, state.Width, state.Height, cell.x, cell.y, moves))
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

	m_LastImpact = Impact{ true, rows, compact, speed, static_cast<int>(m_LastClearedRows.size()),
		static_cast<int>(m_LastClearedRows.size()), landing };
}

void BoardView::SeedMeltAnimations()
{
	TetrisGameState& state = m_Manager.GetState();

	// Hand each block a fire block just burned to the shrink-away animation,
	// burning toward the fire's colour. They leave the block cache (so the
	// sweep that destroys vanished blocks skips them) and the clear-animaton
	// machinery retires them when the burn finishes.
	for (const uint32_t index : state.MeltedCells)
	{
		if (index >= state.Board.size())
			continue;

		const glm::ivec2 cell = state.GetGridPosition(index);
		const uint64_t key = CellKey(static_cast<uint32_t>(cell.x), static_cast<uint32_t>(cell.y));

		const auto block = m_Blocks.find(key);
		if (block == m_Blocks.end())
			continue;  // nothing was drawn there: nothing to burn

		// If a block is already animating out of this cell (a clear, or an
		// earlier melt that has not finished), retire it first. Overwriting the
		// slot would drop its entity from the tracking map and leave a
		// half-shrunk block stranded on screen, no longer on the board.
		const auto inFlight = m_ClearingBlocks.find(key);
		if (inFlight != m_ClearingBlocks.end())
		{
			Forge::Entity stale = inFlight->second.Entity;
			DestroyEntity(stale);
			m_ClearingBlocks.erase(inFlight);
		}

		ClearingBlock cb;
		cb.Entity = block->second;
		cb.Color = block->second.GetComponent<Forge::PBRMaterialComponent>().Albedo;
		cb.From = CellToWorld(static_cast<uint32_t>(cell.x), static_cast<uint32_t>(cell.y));
		cb.To = cb.From;
		cb.TravelDuration = 0.0f;
		cb.Elapsed = 0.0f;
		cb.Duration = kMeltSeconds;
		cb.IsMelt = true;

		RenameEntity(block->second, ClearingBlockName(static_cast<uint32_t>(cell.x), static_cast<uint32_t>(cell.y)));
		m_ClearingBlocks[key] = cb;
		m_Motions.erase(key);
		m_Blocks.erase(block);
	}

	// Each melt is animated once. The melted list lives until the next fire
	// drop, so without clearing it a block that lands in a just-melted cell
	// next frame would be burned a second time.
	state.MeltedCells.clear();
}

void BoardView::AdvanceClearAnimations(float dt)
{
	for (auto it = m_ClearingBlocks.begin(); it != m_ClearingBlocks.end(); )
	{
		ClearingBlock& cb = it->second;
		cb.Elapsed += dt;

		// One clock, two phases: a block that completed the line travels from
		// where the piece drew it into the row it filled, and only then does
		// the row go. A block already in the row travels nowhere, so the clear
		// starts at once. A hard drop's pack runs ahead of both phases, so the
		// row waits out the slide that filled it before it lights up.
		const float local = cb.Elapsed - cb.Delay;
		const float travel = cb.TravelDuration > 0.0f
			? Clamp01(local / cb.TravelDuration)
			: 1.0f;
		const float clear = cb.Duration > 0.0f
			? Clamp01((local - cb.TravelDuration) / cb.Duration)
			: 1.0f;

		if (cb.Entity)
		{
			auto& transform = cb.Entity.GetComponent<Forge::TransformComponent>();
			transform.Translation = glm::mix(cb.From, cb.To, StrikeEase(travel));
			transform.Scale = glm::vec3(kBlockScale * (1.0f - clear), kBlockScale * (1.0f - clear),
				kBlockDepth * (1.0f - clear));

			// The row lights up as it goes out. The flash lives on the
			// emissive term, because pbr.frag multiplies albedo into the
			// texture and adds emissive - a white albedo would wash the tile
			// out, and the tile is the block's surface. A melt burns toward
			// the fire's orange and glows harder than a line-clear flash.
			auto& material = cb.Entity.GetComponent<Forge::PBRMaterialComponent>();
			if (cb.IsMelt)
			{
				material.Emissive = FlashTowardColor(cb.Color, TetrisManager::kFireColor, clear);
				material.EmissiveIntensity = kBlockEmissiveIntensity + kFireMeltGlow * clear;
			}
			else
			{
				material.Emissive = FlashTowardWhite(cb.Color, clear);
			}
		}

		if (cb.Elapsed >= cb.Delay + cb.TravelDuration + cb.Duration)
		{
			Forge::Entity entity = cb.Entity;
			DestroyEntity(entity);
			it = m_ClearingBlocks.erase(it);
		}
		else
		{
			++it;
		}
	}
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