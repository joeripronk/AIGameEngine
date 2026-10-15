#pragma once

#include "TetrisTypes.h"
#include "TetrisManager.h"
#include <memory>
#include <vector>

namespace Forge {

// Tetris game mode component.
struct TetrisGameComponent
{
	// Game manager instance.
	Scope<TetrisManager> Manager;

	// Game settings.
	bool IsRunning = false;
	bool IsPaused = false;
	bool IsGameOver = false;

	// UI settings.
	glm::vec3 BoardPosition = glm::vec3(0.0f, -10.0f, 0.0f);
	float BoardWidth = 10.0f;
	float BoardHeight = 20.0f;
	float BlockSize = 1.0f;

	// Visual effects.
	float ClearAnimationDuration = 0.5f;
	float ClearAnimationTime = 0.0f;

	TetrisGameComponent() = default;
	TetrisGameComponent(const TetrisGameComponent&) = default;
};

// Component for individual tetromino pieces.
struct TetrisPieceComponent
{
	// Piece data.
	TetrominoType Type = TetrominoType::I;
	TetrisPieceState State = TetrisPieceState::Active;

	// Grid position.
	glm::ivec2 GridPosition = glm::ivec2(0, 0);

	// Visual offset from grid position.
	glm::vec3 VisualOffset = glm::vec3(0.0f);

	// Color.
	glm::vec3 Color = glm::vec3(1.0f);

	// Animation.
	float Scale = 1.0f;
	float Alpha = 1.0f;

	TetrisPieceComponent() = default;
	TetrisPieceComponent(const TetrisPieceComponent&) = default;
};

// Component for the game board.
struct TetrisBoardComponent
{
	// Board configuration.
	uint32_t Width = 10;
	uint32_t Height = 20;

	// Board data (flattened 3D array).
	std::vector<uint8_t> Board;

	// Lines to clear.
	std::vector<bool> LinesToClear;

	TetrisBoardComponent() = default;
	TetrisBoardComponent(const TetrisBoardComponent&) = default;
};

// Component for game state display (score, level, etc.).
struct TetrisDisplayComponent
{
	// Score and level.
	int32_t Score = 0;
	int32_t Level = 1;
	int32_t LinesCleared = 0;

	// Combo counter.
	int32_t Combo = 0;

	// Next piece preview.
	TetrominoType NextPiece = TetrominoType::Count;

	// Game state text.
	std::string GameStateText = "Menu";

	TetrisDisplayComponent() = default;
	TetrisDisplayComponent(const TetrisDisplayComponent&) = default;
};

}
