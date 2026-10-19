#include "fgpch.h"

#include "TetrisManager.h"

#include <algorithm>
#include <cassert>

namespace Tetris {

// Static tetromino definitions. `shape` is the rotation-0 layout, with the
// origin at the piece's top-left bounding-box corner.
//
// `color` is the piece's identity colour: the emissive term, the preview and
// the state components read it. The blocks themselves are drawn in the theme
// textures (see `kBlockTheme`), and each of these is the dominant colour of
// the tile that piece uses, so the glow agrees with the surface. They are
// named per palette rather than per classic colour because the tiles the theme
// is built from - sgtcrispy's CC0 "Colored Metal tiles 1b" - have no yellow or
// orange, and a colour no block is painted in is a colour that lies.
const TetrominoDefinition TetrisGameState::kTetrominoDefinitions[] = {
	// I
	{ { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } }, glm::vec3(0.00f, 0.62f, 0.60f) },
	// O
	{ { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } }, glm::vec3(0.85f, 0.00f, 0.80f) },
	// T
	{ { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 1, 1 } }, glm::vec3(0.97f, 0.35f, 0.60f) },
	// S
	{ { { 1, 0 }, { 2, 0 }, { 0, 1 }, { 1, 1 } }, glm::vec3(0.20f, 0.72f, 0.05f) },
	// Z
	{ { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 2, 1 } }, glm::vec3(0.89f, 0.00f, 0.35f) },
	// J
	{ { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }, glm::vec3(0.00f, 0.47f, 0.97f) },
	// L
	{ { { 2, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 } }, glm::vec3(0.41f, 0.27f, 0.99f) },
};

namespace {

// Columns are 10 wide; a piece spawns centred over them.
constexpr int32_t kSpawnRow = 0;

// xorshift32: small, fast, and deterministic given a seed, so a test can
// reproduce an exact piece sequence. std::rand would work too but offers no
// way to control the seed.
uint32_t NextRandom(uint32_t& state)
{
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return state;
}

}  // namespace

const TetrominoDefinition& TetrisGameState::GetDefinition(TetrominoType type)
{
	assert(static_cast<uint8_t>(type) < static_cast<uint8_t>(TetrominoType::Count));
	return kTetrominoDefinitions[static_cast<uint8_t>(type)];
}

void TetrisGameState::InitializeBoard()
{
	Board.assign(static_cast<size_t>(Width) * Height, 0);
}

bool TetrisGameState::IsValidPosition(glm::ivec2 pos) const
{
	return pos.x >= 0 && pos.x < static_cast<int32_t>(Width) &&
		pos.y >= 0 && pos.y < static_cast<int32_t>(Height);
}

uint32_t TetrisGameState::GetBoardIndex(glm::ivec2 pos) const
{
	return static_cast<uint32_t>(pos.y) * Width + static_cast<uint32_t>(pos.x);
}

glm::ivec2 TetrisGameState::GetGridPosition(uint32_t index) const
{
	return glm::ivec2(
		static_cast<int32_t>(index % Width),
		static_cast<int32_t>(index / Width));
}

void TetrisGameState::UpdatePieceBlocks(TetrisPiece& piece) const
{
	GetRotatedBlocks(piece.Type, piece.Rotation, piece.Blocks);
}

bool TetrisGameState::IsRowFull(uint32_t row) const
{
	for (uint32_t col = 0; col < Width; ++col)
	{
		const uint32_t index = row * Width + col;
		if (index >= Board.size() || Board[index] == 0)
			return false;
	}
	return true;
}

const char* ToString(TetrisGameState::State state)
{
	switch (state)
	{
	case TetrisGameState::State::Menu: return "Menu";
	case TetrisGameState::State::Playing: return "Playing";
	case TetrisGameState::State::Paused: return "Paused";
	case TetrisGameState::State::GameOver: return "GameOver";
	}
	return "Unknown";
}

void GetRotatedBlocks(TetrominoType type, uint8_t rotation, glm::ivec2 outBlocks[4])
{
	const TetrominoDefinition& def = TetrisGameState::GetDefinition(type);
	rotation &= 3u;

	// Rotate about the centre of the piece's bounding box, then shift the
	// result so its minimum corner is the origin.
	//
	// The shift is what keeps every offset non-negative. Rotating about the
	// origin alone puts half of the blocks at negative coordinates, so an I
	// piece rotated to vertical would start a row *above* the board and be
	// unplaceable - rotation would work everywhere except where it matters.
	int32_t minX = def.shape[0].x, maxX = def.shape[0].x;
	int32_t minY = def.shape[0].y, maxY = def.shape[0].y;
	for (int i = 1; i < 4; ++i)
	{
		minX = std::min(minX, def.shape[i].x);
		maxX = std::max(maxX, def.shape[i].x);
		minY = std::min(minY, def.shape[i].y);
		maxY = std::max(maxY, def.shape[i].y);
	}
	const glm::ivec2 centre((minX + maxX) / 2, (minY + maxY) / 2);

	for (int i = 0; i < 4; ++i)
	{
		glm::ivec2 p = def.shape[i];
		for (uint8_t r = 0; r < rotation; ++r)
		{
			const glm::ivec2 d = p - centre;
			p = centre + glm::ivec2(d.y, -d.x);  // 90 degrees clockwise
		}
		outBlocks[i] = p;
	}

	int32_t lowX = outBlocks[0].x, lowY = outBlocks[0].y;
	for (int i = 1; i < 4; ++i)
	{
		lowX = std::min(lowX, outBlocks[i].x);
		lowY = std::min(lowY, outBlocks[i].y);
	}
	for (int i = 0; i < 4; ++i)
		outBlocks[i] -= glm::ivec2(lowX, lowY);
}

// ---------------------------------------------------------------------------
// TetrisManager
// ---------------------------------------------------------------------------

void TetrisManager::Initialize()
{
	Reset();
}

void TetrisManager::Reset()
{
	m_State = TetrisGameState();
	m_State.InitializeBoard();
	m_State.CurrentState = TetrisGameState::State::Menu;
	m_State.PieceDropInterval = GetDropInterval();

	m_MoveLeftHeld = false;
	m_MoveRightHeld = false;
	m_SoftDropHeld = false;
	m_MoveRepeatTimer = kMoveDelay;
	m_SoftDropRepeatTimer = kSoftDropRepeat;
}

void TetrisManager::StartGame()
{
	Reset();

	m_State.CurrentState = TetrisGameState::State::Playing;
	m_State.NextPiece = CreatePiece(NextRandomType(TetrominoType::Count));
	m_State.CurrentPiece = CreatePiece(NextRandomType(m_State.NextPiece.Type));
}

void TetrisManager::TogglePause()
{
	if (m_State.CurrentState == TetrisGameState::State::Playing)
		m_State.CurrentState = TetrisGameState::State::Paused;
	else if (m_State.CurrentState == TetrisGameState::State::Paused)
		m_State.CurrentState = TetrisGameState::State::Playing;
}

void TetrisManager::Update(float deltaTime)
{
	if (m_State.CurrentState != TetrisGameState::State::Playing)
		return;

	if (deltaTime > 0.0f)
	{
		m_State.PieceDropTimer += deltaTime;
		if (m_State.PieceDropTimer >= m_State.PieceDropInterval)
		{
			// Only drop once per tick even after a long frame: catching up would
			// move the piece further than the elapsed time justifies.
			m_State.PieceDropTimer = 0.0f;
			if (!SoftDrop())
				LockPiece();
		}
	}

	UpdateHeldActions(deltaTime);
}

void TetrisManager::UpdateHeldActions(float deltaTime)
{
	// The timers count *down* to the next repeat, so arming one with the
	// initial delay on a fresh press means what it says.
	//
	// Repeats are capped per frame: a frame that stalls for a second would
	// otherwise sweep the piece the length of the board in one step, which
	// reads as a teleport rather than as input.
	if (m_MoveLeftHeld || m_MoveRightHeld)
	{
		// Left and right share a timer, so holding both resolves to a single
		// repeat rather than two that cancel.
		m_MoveRepeatTimer -= deltaTime;
		int repeats = 0;
		while (m_MoveRepeatTimer <= 0.0f && repeats < kMaxRepeatsPerFrame)
		{
			m_MoveRepeatTimer += kMoveRepeat;
			MovePiece(m_MoveLeftHeld ? TetrisDirection::Left : TetrisDirection::Right);
			++repeats;
		}
	}

	if (m_SoftDropHeld)
	{
		m_SoftDropRepeatTimer -= deltaTime;
		int repeats = 0;
		while (m_SoftDropRepeatTimer <= 0.0f && repeats < kMaxRepeatsPerFrame)
		{
			m_SoftDropRepeatTimer += kSoftDropRepeat;
			SoftDrop();
			++repeats;
		}
	}
}

void TetrisManager::HandleAction(Action action, bool pressed)
{
	// Held actions only need their state tracking; the move itself happens in
	// Update so it repeats on a timer instead of once per key-repeat event.
	if (!pressed)
	{
		switch (action)
		{
		case Action::MoveLeft: m_MoveLeftHeld = false; break;
		case Action::MoveRight: m_MoveRightHeld = false; break;
		case Action::SoftDrop: m_SoftDropHeld = false; break;
		default: return;
		}
		// Release while playing must not leave a piece mid-repeat.
		m_MoveRepeatTimer = kMoveDelay;
		m_SoftDropRepeatTimer = kSoftDropRepeat;
		return;
	}

	switch (action)
	{
	case Action::MoveLeft:
	case Action::MoveRight:
		if (m_State.CurrentState != TetrisGameState::State::Playing)
			return;
		// Move on the press itself, then let the repeat timer take over.
		if (action == Action::MoveLeft)
		{
			m_MoveLeftHeld = true;
			MovePiece(TetrisDirection::Left);
		}
		else
		{
			m_MoveRightHeld = true;
			MovePiece(TetrisDirection::Right);
		}
		m_MoveRepeatTimer = kMoveDelay;
		return;

	case Action::SoftDrop:
		if (m_State.CurrentState != TetrisGameState::State::Playing)
			return;
		m_SoftDropHeld = true;
		SoftDrop();
		m_SoftDropRepeatTimer = kSoftDropRepeat;
		return;

	case Action::HardDrop:
		HardDrop();
		return;

	case Action::RotateCW:
		RotatePiece(true);
		return;

	case Action::RotateCCW:
		RotatePiece(false);
		return;

	case Action::TogglePause:
		// Pause is the one action that must work while paused, so it is not
		// gated on the current state the way the others are.
		TogglePause();
		return;

	case Action::Restart:
		// R restarts whether the game is running, paused or over.
		StartGame();
		return;

	case Action::None:
		return;
	}
}

const TetrominoDefinition& TetrisManager::GetTetrominoDefinition(TetrominoType type)
{
	return TetrisGameState::GetDefinition(type);
}

TetrisPiece TetrisManager::CreatePiece(TetrominoType type) const
{
	const TetrominoDefinition& def = GetTetrominoDefinition(type);

	int32_t minX = def.shape[0].x, maxX = def.shape[0].x;
	for (int i = 1; i < 4; ++i)
	{
		minX = std::min(minX, def.shape[i].x);
		maxX = std::max(maxX, def.shape[i].x);
	}
	const int32_t span = maxX - minX + 1;

	TetrisPiece piece;
	piece.Type = type;
	piece.State = TetrisPieceState::Active;
	piece.Position = glm::ivec2((static_cast<int32_t>(m_State.Width) - span) / 2, kSpawnRow);
	piece.Rotation = 0;
	piece.Color = glm::vec4(def.color, 1.0f);
	m_State.UpdatePieceBlocks(piece);
	return piece;
}

bool TetrisManager::RotatePiece(bool clockwise)
{
	if (m_State.CurrentState != TetrisGameState::State::Playing)
		return false;
	if (m_State.CurrentPiece.State != TetrisPieceState::Active)
		return false;

	const uint8_t nextRotation = static_cast<uint8_t>((m_State.CurrentPiece.Rotation + (clockwise ? 1u : 3u)) & 3u);

	// Kick the piece sideways (or up) until the rotated shape fits. A rotation
	// against a wall or the floor is normal play, not an error.
	for (const auto& offset : kWallKickOffsets)
	{
		const glm::ivec2 candidate = m_State.CurrentPiece.Position + glm::ivec2(offset[0], offset[1]);
		if (!CanPlaceAt(candidate, nextRotation))
			continue;

		m_State.CurrentPiece.Position = candidate;
		m_State.CurrentPiece.Rotation = nextRotation;
		m_State.UpdatePieceBlocks(m_State.CurrentPiece);
		return true;
	}

	return false;
}

bool TetrisManager::MovePiece(TetrisDirection direction)
{
	if (m_State.CurrentPiece.State != TetrisPieceState::Active)
		return false;

	glm::ivec2 moveDir(0, 0);
	switch (direction)
	{
	case TetrisDirection::Left: moveDir = glm::ivec2(-1, 0); break;
	case TetrisDirection::Right: moveDir = glm::ivec2(1, 0); break;
	default: return false;
	}

	const glm::ivec2 target = m_State.CurrentPiece.Position + moveDir;
	if (!CanPlaceAt(target, m_State.CurrentPiece.Rotation))
		return false;

	m_State.CurrentPiece.Position = target;
	return true;
}

bool TetrisManager::SoftDrop()
{
	if (m_State.CurrentPiece.State != TetrisPieceState::Active)
		return false;

	const glm::ivec2 target = m_State.CurrentPiece.Position + glm::ivec2(0, 1);
	if (!CanPlaceAt(target, m_State.CurrentPiece.Rotation))
		return false;

	m_State.CurrentPiece.Position = target;
	// A manual drop counts as a gravity tick, so holding down does not also
	// stack a full drop interval on top of the repeats.
	m_State.PieceDropTimer = 0.0f;
	return true;
}

void TetrisManager::HardDrop()
{
	if (m_State.CurrentPiece.State != TetrisPieceState::Active)
		return;

	while (SoftDrop())
	{
	}

	LockPiece();
}

glm::ivec2 TetrisManager::GetGhostPosition() const
{
	glm::ivec2 ghost = m_State.CurrentPiece.Position;
	while (true)
	{
		const glm::ivec2 candidate = ghost + glm::ivec2(0, 1);
		if (!CanPlaceAt(candidate, m_State.CurrentPiece.Rotation))
			break;
		ghost = candidate;
	}
	return ghost;
}

bool TetrisManager::CanMoveTo(glm::ivec2 pos) const
{
	return CanPlaceAt(pos, m_State.CurrentPiece.Rotation);
}

bool TetrisManager::CanPlaceAt(glm::ivec2 pos, uint8_t rotation) const
{
	glm::ivec2 blocks[4];
	GetRotatedBlocks(m_State.CurrentPiece.Type, rotation, blocks);

	for (const glm::ivec2& block : blocks)
	{
		const glm::ivec2 blockPos = pos + block;
		if (!m_State.IsValidPosition(blockPos))
			return false;

		const uint32_t index = m_State.GetBoardIndex(blockPos);
		if (index < m_State.Board.size() && m_State.Board[index] != 0)
			return false;
	}

	return true;
}

void TetrisManager::LockPiece()
{
	const TetrisPiece& piece = m_State.CurrentPiece;

	// A piece that cannot legally exist must not be written: an out-of-range
	// cell would wrap around into a different row.
	if (!CanPlaceAt(piece.Position, piece.Rotation))
	{
		m_State.CurrentState = TetrisGameState::State::GameOver;
		m_State.HasGameOver = true;
		return;
	}

	for (const glm::ivec2& block : piece.Blocks)
	{
		const uint32_t index = m_State.GetBoardIndex(piece.Position + block);
		if (index < m_State.Board.size())
			m_State.Board[index] = static_cast<uint8_t>(piece.Type) + 1;
	}

	ClearLines();
	SpawnNextPiece();
	CheckGameOver();
}

void TetrisManager::SpawnNextPiece()
{
	m_State.CurrentPiece = m_State.NextPiece;
	m_State.CurrentPiece.State = TetrisPieceState::Active;
	m_State.PieceDropTimer = 0.0f;
	m_State.NextPiece = CreatePiece(NextRandomType(m_State.CurrentPiece.Type));
}

void TetrisManager::CheckGameOver()
{
	if (CanPlaceAt(m_State.CurrentPiece.Position, m_State.CurrentPiece.Rotation))
		return;

	m_State.CurrentState = TetrisGameState::State::GameOver;
	m_State.HasGameOver = true;
}

void TetrisManager::ClearLines()
{
	uint32_t linesCleared = 0;

	// Walk the board upwards from the bottom, copying every row that is not
	// full down over the first row cleared beneath it. Copying from the bottom
	// up is what makes this safe in place: the destination row is always at or
	// below the source row, so no row is overwritten before it has been read.
	//
	// Clearing alone is not enough. Zeroing the full rows leaves a hole where
	// the stack used to be and everything above it hanging in mid air, so the
	// board never fills from the bottom and the game cannot be lost.
	uint32_t dst = m_State.Height;
	for (uint32_t src = m_State.Height; src-- > 0;)
	{
		if (m_State.IsRowFull(src))
		{
			++linesCleared;
			continue;
		}

		--dst;
		if (dst == src)
			continue;

		for (uint32_t col = 0; col < m_State.Width; ++col)
		{
			const uint32_t to = dst * m_State.Width + col;
			const uint32_t from = src * m_State.Width + col;
			if (to < m_State.Board.size() && from < m_State.Board.size())
				m_State.Board[to] = m_State.Board[from];
		}
	}

	// Whatever is left at the top is now empty space.
	for (uint32_t row = 0; row < dst; ++row)
	{
		for (uint32_t col = 0; col < m_State.Width; ++col)
		{
			const uint32_t index = row * m_State.Width + col;
			if (index < m_State.Board.size())
				m_State.Board[index] = 0;
		}
	}

	if (linesCleared == 0)
	{
		m_State.Combo = 0;
		return;
	}

	m_State.Score += GetLineScore(linesCleared, static_cast<uint32_t>(m_State.Level), static_cast<uint32_t>(m_State.Combo));
	++m_State.Combo;

	m_State.LinesCleared += static_cast<int32_t>(linesCleared);
	m_State.Level = m_State.LinesCleared / 10 + 1;
	m_State.PieceDropInterval = GetDropInterval();
}

int32_t TetrisManager::GetLineScore(uint32_t lines, uint32_t level, uint32_t combo)
{
	// Classic scoring: 40 / 100 / 300 / 1200, scaled by level, plus a flat
	// bonus for consecutive clears.
	static const uint32_t kScoreTable[] = { 0, 40, 100, 300, 1200 };

	if (lines > 4)
		lines = 4;

	const int32_t baseScore = static_cast<int32_t>(kScoreTable[lines]) * static_cast<int32_t>(level);
	const int32_t comboBonus = static_cast<int32_t>(combo * 50);

	return baseScore + comboBonus;
}

float TetrisManager::GetDropInterval() const
{
	// Faster every level, with a floor: an unbounded subtraction eventually
	// reaches zero or below, at which point the piece stops moving at all.
	const float baseInterval = 0.8f;
	const float levelModifier = static_cast<float>(m_State.Level - 1) * 0.05f;
	return std::max(0.05f, baseInterval - levelModifier);
}

TetrominoType TetrisManager::NextRandomType(TetrominoType lastType)
{
	// Weighted bag: O and the flat-sided pieces are rarer, which is the usual
	// way to keep a piece randomiser from handing out awkward stacks.
	static const uint32_t kWeights[] = { 2, 1, 2, 2, 2, 1, 1 };

	uint32_t totalWeight = 0;
	for (uint32_t w : kWeights)
		totalWeight += w;

	const auto rollWeighted = [this, totalWeight]() {
		const uint32_t roll = NextRandom(m_RandomState) % totalWeight;
		uint32_t cumulative = 0;
		for (uint32_t i = 0; i < static_cast<uint32_t>(TetrominoType::Count); ++i)
		{
			cumulative += kWeights[i];
			if (roll < cumulative)
				return static_cast<TetrominoType>(i);
		}
		return TetrominoType::I;
	};

	TetrominoType type = rollWeighted();

	// Halve the chance of the same piece back to back: a repeat is accepted
	// only on a coin flip, so it happens half as often as the weights allow.
	// The coin is a full-range comparison on the generator's output - a single
	// xorshift bit is correlated run to run, so a coin built from one bit is
	// not a coin. The re-roll cannot loop forever - the weights cover all
	// seven types.
	if (type == lastType && lastType != TetrominoType::Count
		&& NextRandom(m_RandomState) < 0x80000000u)
	{
		do
			type = rollWeighted();
		while (type == lastType);
	}

	return type;
}

}  // namespace Tetris