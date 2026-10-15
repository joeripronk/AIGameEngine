#include "TetrisManager.h"
#include "Forge/Scene/Components.h"
#include <GLFW/glfw3.h>

namespace Forge {

// Static tetromino definitions.
const TetrominoDefinition TetrisGameState::kTetrominoDefinitions[] = {
	// I
	{ { {0, 0}, {1, 0}, {2, 0}, {3, 0}}, glm::vec3(0.0f, 1.0f, 1.0f) },
	// O
	{ { {0, 0}, {1, 0}, {0, 1}, {1, 1}}, glm::vec3(1.0f, 1.0f, 0.0f) },
	// T
	{ { {0, 0}, {1, 0}, {2, 0}, {1, 1}}, glm::vec3(1.0f, 0.0f, 1.0f) },
	// S
	{ { {1, 0}, {2, 0}, {0, 1}, {1, 1}}, glm::vec3(0.0f, 1.0f, 0.0f) },
	// Z
	{ { {0, 0}, {1, 0}, {1, 1}, {2, 1}}, glm::vec3(1.0f, 0.0f, 0.0f) },
	// J
	{ { {0, 0}, {0, 1}, {1, 1}, {2, 1}}, glm::vec3(0.0f, 0.0f, 1.0f) },
	// L
	{ { {2, 0}, {0, 1}, {1, 1}, {2, 1}}, glm::vec3(1.0f, 0.5f, 0.0f) },
};

const TetrominoDefinition& TetrisGameState::GetDefinition(TetrominoType type)
{
	return kTetrominoDefinitions[static_cast<uint8_t>(type)];
}

TetrominoDefinition TetrisGameState::CreateDefinition(TetrominoType type)
{
	return GetDefinition(type);
}

void TetrisGameState::InitializeBoard()
{
	Board.assign(Width * Height, 0);
	LinesToClear.assign(Height, false);
}

bool TetrisGameState::IsValidPosition(glm::ivec2 pos) const
{
	return pos.x >= 0 && pos.x < static_cast<int>(Width) && pos.y >= 0 && pos.y < static_cast<int>(Height);
}

bool TetrisGameState::IsWithinBounds(glm::ivec2 pos) const
{
	return IsValidPosition(pos);
}

uint32_t TetrisGameState::GetBoardIndex(glm::ivec2 pos) const
{
	return static_cast<uint32_t>(pos.y) * Width + static_cast<uint32_t>(pos.x);
}

glm::ivec2 TetrisGameState::GetGridPosition(uint32_t index) const
{
	return glm::ivec2(
		static_cast<int32_t>(index % Width),
		static_cast<int32_t>(index / Width)
	);
}

void TetrisGameState::StartGame()
{
	CurrentState = State::Playing;
}

float TetrisGameState::GetDropInterval() const
{
	float baseInterval = 0.8f;
	float levelModifier = static_cast<float>(Level - 1) * 0.05f;
	return baseInterval - levelModifier;
}

// ---------------------------------------------------------------------------
// TetrisManager implementation
// ---------------------------------------------------------------------------

void TetrisManager::Initialize()
{
	m_TetrominoDefinitions.reserve(static_cast<uint32_t>(TetrominoType::Count));
	for (uint32_t i = 0; i < static_cast<uint32_t>(TetrominoType::Count); ++i)
	{
		m_TetrominoDefinitions.push_back(TetrisGameState::CreateDefinition(static_cast<TetrominoType>(i)));
	}
}

void TetrisManager::Reset()
{
	m_State = TetrisGameState();
	m_State.InitializeBoard();
	m_State.StartGame();
	m_State.CurrentState = TetrisGameState::State::Menu;
	m_State.Score = 0;
	m_State.Level = 1;
	m_State.LinesCleared = 0;
	m_State.Combo = 0;
	m_State.PieceDropTimer = 0.0f;
	m_State.PieceDropInterval = m_State.GetDropInterval();
	m_State.HasGameOver = false;
	m_State.GameOverTimer = 0.0f;

	m_CurrentDelay = 0.0f;
}

void TetrisManager::StartGame()
{
	m_State.StartGame();
	m_State.CurrentState = TetrisGameState::State::Playing;

	// Create initial pieces.
	m_State.CurrentPiece = CreatePiece(GetRandomTetromino());
	m_State.NextPiece = CreatePiece(GetRandomTetromino());
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

	// Update piece drop timer.
	m_State.PieceDropTimer += deltaTime;
	if (m_State.PieceDropTimer >= m_State.PieceDropInterval)
	{
		m_State.PieceDropTimer = 0.0f;

		// Try to move piece down.
		glm::ivec2 newPos = m_State.CurrentPiece.Position + glm::ivec2(0, TetrisDirection::Down);
		if (CanMoveTo(newPos))
		{
			m_State.CurrentPiece.Position = newPos;
		}
		else
		{
			// Lock piece.
			LockPiece();
		}
	}

	// Handle continuous input.
	m_CurrentDelay += deltaTime;
	if (m_CurrentDelay >= m_MoveDelay)
	{
		if (m_LeftPressed)
		{
			glm::ivec2 newPos = m_State.CurrentPiece.Position + glm::ivec2(TetrisDirection::Left, 0);
			if (CanMoveTo(newPos))
				m_State.CurrentPiece.Position = newPos;
		}
		if (m_RightPressed)
		{
			glm::ivec2 newPos = m_State.CurrentPiece.Position + glm::ivec2(TetrisDirection::Right, 0);
			if (CanMoveTo(newPos))
				m_State.CurrentPiece.Position = newPos;
		}
		if (m_DownPressed)
		{
			SoftDrop();
			m_CurrentDelay = 0.0f;
		}
	}
}

void TetrisManager::HandleInput(int keyCode, bool pressed)
{
	if (pressed)
	{
		switch (keyCode)
		{
		case GLFW_KEY_LEFT: m_LeftPressed = true; break;
		case GLFW_KEY_RIGHT: m_RightPressed = true; break;
		case GLFW_KEY_DOWN: m_DownPressed = true; break;
		case GLFW_KEY_UP: m_RotatePressed = true; break;
		case GLFW_KEY_SPACE: HardDrop(); break;
		case GLFW_KEY_P: TogglePause(); break;
		case GLFW_KEY_R: if (m_State.HasGameOver) Reset(); break;
		}
	}
	else
	{
		switch (keyCode)
		{
		case GLFW_KEY_LEFT: m_LeftPressed = false; break;
		case GLFW_KEY_RIGHT: m_RightPressed = false; break;
		case GLFW_KEY_DOWN: m_DownPressed = false; break;
		}
	}

	// Handle single-press actions.
	if (pressed)
	{
		if (keyCode == GLFW_KEY_UP)
			RotatePiece();
	}
}

TetrominoType TetrisManager::GetRandomTetromino() const
{
	// Weighted random for better gameplay.
	uint32_t weights[] = { 2, 1, 2, 2, 2, 1, 1 }; // I, O, T, S, Z, J, L
	uint32_t totalWeight = 0;
	for (uint32_t w : weights)
		totalWeight += w;

	uint32_t random = static_cast<uint32_t>(std::rand() % totalWeight);
	uint32_t cumulative = 0;
	for (uint32_t i = 0; i < static_cast<uint32_t>(TetrominoType::Count); ++i)
	{
		cumulative += weights[i];
		if (random < cumulative)
			return static_cast<TetrominoType>(i);
	}
	return TetrominoType::I;
}

const TetrominoDefinition& TetrisManager::GetTetrominoDefinition(TetrominoType type)
{
	return TetrisGameState::GetDefinition(type);
}

TetrisPiece TetrisManager::CreatePiece(TetrominoType type) const
{
	TetrisPiece piece;
	piece.Type = type;
	piece.State = TetrisPieceState::Active;
	piece.Position = glm::ivec2(3, 0); // Start near top center
	piece.Rotation = 0;
	piece.DropSpeed = 1.0f;

	// Set color based on tetromino type.
	const auto& def = GetTetrominoDefinition(type);
	piece.Color = glm::vec4(def.color, 1.0f);

	return piece;
}

void TetrisManager::RotatePiece()
{
	if (m_State.CurrentPiece.State != TetrisPieceState::Active)
		return;

	if (CanRotate())
	{
		m_State.CurrentPiece.Rotation = (m_State.CurrentPiece.Rotation + 1) % 4;
		if (OnPieceLock)
			OnPieceLock();
	}
}

void TetrisManager::MovePiece(TetrisDirection direction)
{
	if (m_State.CurrentPiece.State != TetrisPieceState::Active)
		return;

	glm::ivec2 moveDir;
	switch (direction)
	{
	case TetrisDirection::Left: moveDir = glm::ivec2(-1, 0); break;
	case TetrisDirection::Right: moveDir = glm::ivec2(1, 0); break;
	default: moveDir = glm::ivec2(0, 0); break;
	}

	glm::ivec2 newPos = m_State.CurrentPiece.Position + moveDir;
	if (CanMoveTo(newPos))
	{
		m_State.CurrentPiece.Position = newPos;
	}
}

void TetrisManager::SoftDrop()
{
	if (m_State.CurrentPiece.State != TetrisPieceState::Active)
		return;

	glm::ivec2 newPos = m_State.CurrentPiece.Position + glm::ivec2(0, 1);
	if (CanMoveTo(newPos))
	{
		m_State.CurrentPiece.Position = newPos;
		m_State.PieceDropTimer = 0.0f; // Reset drop timer
	}
}

void TetrisManager::HardDrop()
{
	if (m_State.CurrentPiece.State != TetrisPieceState::Active)
		return;

	// Drop piece to bottom.
	while (true)
	{
		glm::ivec2 newPos = m_State.CurrentPiece.Position + glm::ivec2(0, 1);
		if (!CanMoveTo(newPos))
			break;
		m_State.CurrentPiece.Position = newPos;
	}

	// Lock piece.
	LockPiece();
}

glm::ivec2 TetrisManager::GetGhostPosition() const
{
	glm::ivec2 ghost = m_State.CurrentPiece.Position;

	while (true)
	{
		glm::ivec2 newPos = ghost + glm::ivec2(0, 1);
		if (!CanMoveTo(newPos))
			break;
		ghost = newPos;
	}

	return ghost;
}

bool TetrisManager::CanMoveTo(glm::ivec2 pos) const
{
	const auto& piece = m_State.CurrentPiece;
	const auto& def = TetrisGameState::GetDefinition(piece.Type);

	for (const auto& block : def.shape)
	{
		glm::ivec2 blockPos = pos + block;
		if (!m_State.IsWithinBounds(blockPos))
			return false;

		// Check if position is occupied.
		uint32_t index = m_State.GetBoardIndex(blockPos);
		if (index < m_State.Board.size() && m_State.Board[index] != 0)
			return false;
	}

	return true;
}

void TetrisManager::LockPiece()
{
	auto& piece = m_State.CurrentPiece;
	const auto& def = TetrisGameState::GetDefinition(piece.Type);

	// Place piece on board.
	for (const auto& block : def.shape)
	{
		glm::ivec2 blockPos = piece.Position + block;
		uint32_t index = m_State.GetBoardIndex(blockPos);

		if (index < m_State.Board.size())
		{
			m_State.Board[index] = static_cast<uint8_t>(piece.Type) + 1;
		}
	}

	piece.State = TetrisPieceState::Fixed;

	if (OnPieceLock)
		OnPieceLock();

	// Clear lines.
	ClearLines();

	// Spawn next piece.
	m_State.CurrentPiece = m_State.NextPiece;
	m_State.CurrentPiece.State = TetrisPieceState::Active;
	m_State.CurrentPiece.Position = glm::ivec2(3, 0);
	m_State.CurrentPiece.Rotation = 0;
	m_State.PieceDropTimer = 0.0f;

	// Check game over.
	if (!CanMoveTo(m_State.CurrentPiece.Position))
	{
		m_State.CurrentState = TetrisGameState::State::GameOver;
		m_State.HasGameOver = true;
		if (OnGameOver)
			OnGameOver(m_State.Score);
	}
}

void TetrisManager::ClearLines()
{
	uint32_t linesCleared = 0;

	// Check each row.
	for (uint32_t row = 0; row < m_State.Height; ++row)
	{
		bool lineFull = true;
		for (uint32_t col = 0; col < m_State.Width; ++col)
		{
			uint32_t index = row * m_State.Width + col;
			if (index >= m_State.Board.size() || m_State.Board[index] == 0)
			{
				lineFull = false;
				break;
			}
		}

		if (lineFull)
		{
			m_State.LinesToClear[row] = true;
			linesCleared++;
		}
	}

	if (linesCleared > 0)
	{
		// Calculate score.
		int32_t score = GetLineScore(linesCleared, m_State.Level, m_State.Combo);
		m_State.Score += score;
		m_State.Combo++;

		// Update level based on lines cleared.
		m_State.LinesCleared += linesCleared;
		m_State.Level = static_cast<int32_t>(m_State.LinesCleared / 10) + 1;
		m_State.PieceDropInterval = GetDropInterval();

		if (OnLinesCleared)
			OnLinesCleared(linesCleared);

		// Clear the lines from board.
		for (uint32_t row = 0; row < m_State.Height; ++row)
		{
			if (m_State.LinesToClear[row])
			{
				for (uint32_t col = 0; col < m_State.Width; ++col)
				{
					uint32_t index = row * m_State.Width + col;
					if (index < m_State.Board.size())
						m_State.Board[index] = 0;
				}
			}
		}

		// Reset line clear flags.
		for (auto flag : m_State.LinesToClear)
			flag = false;
	}
	else
	{
		m_State.Combo = 0;
	}
}

int32_t TetrisManager::GetLineScore(uint32_t lines, uint32_t level, uint32_t combo) const
{
	// Classic scoring system.
	static const uint32_t kScoreTable[] = { 0, 40, 100, 300, 1200 };

	if (lines > 4)
		lines = 4;

	int32_t baseScore = kScoreTable[lines] * level;
	int32_t comboBonus = static_cast<int32_t>(combo * 50);

	return baseScore + comboBonus;
}

float TetrisManager::GetDropInterval() const
{
	// Decrease drop interval as level increases.
	float baseInterval = 0.8f;
	float levelModifier = static_cast<float>(m_State.Level - 1) * 0.05f;
	return baseInterval - levelModifier;
}

bool TetrisManager::CanRotate() const
{
	const auto& piece = m_State.CurrentPiece;
	const auto& def = TetrisGameState::GetDefinition(piece.Type);

	// Try rotating each block.
	for (const auto& block : def.shape)
	{
		glm::ivec2 rotatedPos = piece.Position + block;
		// Apply rotation offset.
		rotatedPos.x += 1;
		rotatedPos.y += 1;

		if (!m_State.IsWithinBounds(rotatedPos))
			return false;

		uint32_t index = m_State.GetBoardIndex(rotatedPos);
		if (index < m_State.Board.size() && m_State.Board[index] != 0)
			return false;
	}

	return true;
}

glm::ivec2 TetrisManager::RotateShape(const glm::ivec2& pos, uint8_t rotation) const
{
	// Simple 90° rotation around origin.
	glm::ivec2 result(pos.x, pos.y);
	switch (rotation)
	{
	case 0: break; // No rotation
	case 1: // 90° CW
		std::swap(result.x, result.y);
		result.x = -result.x;
		break;
	case 2: // 180°
		result.x = -result.x;
		result.y = -result.y;
		break;
	case 3: // 270° CW
		std::swap(result.x, result.y);
		result.y = -result.y;
		break;
	}
	return result;
}

std::vector<glm::ivec2> TetrisManager::GetShapePositions(uint8_t rotation) const
{
	const auto& def = TetrisGameState::GetDefinition(m_State.CurrentPiece.Type);
	std::vector<glm::ivec2> positions;

	for (const auto& block : def.shape)
	{
		glm::ivec2 pos = block;
		// Apply rotation.
		for (uint8_t i = 0; i < rotation; ++i)
		{
			std::swap(pos.x, pos.y);
			pos.x = -pos.x;
		}
		positions.push_back(pos);
	}

	return positions;
}

bool TetrisManager::ApplyWallKick(int32_t offset)
{
	// Try offset positions.
	glm::ivec2 testPos = m_State.CurrentPiece.Position + glm::ivec2(offset, 0);
	glm::ivec2 oldPos = m_State.CurrentPiece.Position;

	m_State.CurrentPiece.Position = testPos;
	if (CanMoveTo(testPos))
	{
		m_State.CurrentPiece.Rotation = (m_State.CurrentPiece.Rotation + 1) % 4;
		return true;
	}

	// Restore.
	m_State.CurrentPiece.Position = oldPos;
	return false;
}

void TetrisGameState::MovePiece(TetrisDirection direction)
{
	glm::ivec2 moveDir;
	switch (direction)
	{
	case TetrisDirection::Left: moveDir = glm::ivec2(-1, 0); break;
	case TetrisDirection::Right: moveDir = glm::ivec2(1, 0); break;
	default: moveDir = glm::ivec2(0, 0); break;
	}

	glm::ivec2 newPos = CurrentPiece.Position + moveDir;
	if (IsValidPosition(newPos))
	{
		CurrentPiece.Position = newPos;
	}
}

void TetrisGameState::RotatePiece()
{
	CurrentPiece.Rotation = (CurrentPiece.Rotation + 1) % 4;
}

void TetrisGameState::HardDrop()
{
	while (true)
	{
		glm::ivec2 newPos = CurrentPiece.Position + glm::ivec2(0, 1);
		if (!IsValidPosition(newPos))
			break;
		CurrentPiece.Position = newPos;
	}
}

void TetrisGameState::Update(float deltaTime)
{
	// Update piece drop timer.
	PieceDropTimer += deltaTime;
	if (PieceDropTimer >= PieceDropInterval)
	{
		PieceDropTimer = 0.0f;
	}
}

} // namespace Forge
