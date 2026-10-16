#pragma once

#include "TetrisTypes.h"

#include <string>
#include <vector>

namespace Tetris {

// ECS components carrying the game's state in the scene.
//
// The rules live in TetrisManager and these are a read-only mirror of them,
// one per frame. They exist so a tool or an AI agent driving the editor can
// inspect a running game - board contents, score, what is falling - without
// reaching into the manager that owns it.
//
// Like everything else in this project they are deliberately *not* part of the
// engine's component set: a game defines its own components, and the engine
// has no reason to know what a tetromino is.

// Root of the game. Marks the entity that owns the rest of the playfield.
struct TetrisGameComponent
{
	bool IsRunning = false;
	bool IsPaused = false;
	bool IsGameOver = false;

	// Playfield placement in world units, so a view can lay itself out without
	// knowing the grid-to-world mapping.
	glm::vec3 BoardPosition = glm::vec3(0.0f, -10.0f, 0.0f);
	float BoardWidth = 10.0f;
	float BoardHeight = 20.0f;
	float BlockSize = 1.0f;

	TetrisGameComponent() = default;
	TetrisGameComponent(const TetrisGameComponent&) = default;
};

// The board itself: every cell, 0 = empty.
struct TetrisBoardComponent
{
	uint32_t Width = 10;
	uint32_t Height = 20;
	std::vector<uint8_t> Board;

	TetrisBoardComponent() = default;
	TetrisBoardComponent(const TetrisBoardComponent&) = default;
};

// The falling piece, as one entity rather than one per block.
struct TetrisPieceComponent
{
	TetrominoType Type = TetrominoType::I;
	TetrisPieceState State = TetrisPieceState::Active;
	glm::ivec2 GridPosition = glm::ivec2(0, 0);
	uint8_t Rotation = 0;
	glm::vec3 Color = glm::vec3(1.0f);

	TetrisPieceComponent() = default;
	TetrisPieceComponent(const TetrisPieceComponent&) = default;
};

// Score read-out.
struct TetrisDisplayComponent
{
	int32_t Score = 0;
	int32_t Level = 1;
	int32_t LinesCleared = 0;
	int32_t Combo = 0;
	TetrominoType NextPiece = TetrominoType::Count;
	std::string GameStateText = "Menu";

	TetrisDisplayComponent() = default;
	TetrisDisplayComponent(const TetrisDisplayComponent&) = default;
};

}  // namespace Tetris