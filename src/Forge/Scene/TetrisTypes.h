#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <optional>

namespace Forge {

// The 7 standard tetrominoes.
enum class TetrominoType : uint8_t
{
	I = 0,
	O,
	T,
	S,
	Z,
	J,
	L,
	Count
};

// Direction constants for piece movement.
enum class TetrisDirection : int8_t
{
	Left = -1,
	Right = 1,
	Down = 1,
	Up = -1
};

// Piece states.
enum class TetrisPieceState : uint8_t
{
	Active = 0,
	Fixed,  // Locked into the board
	Ghost,  // Shadow showing where piece will land
	Destroyed
};

// Tetris piece definition (shape + color).
struct TetrominoDefinition
{
	glm::ivec2 shape[4];  // 4 blocks of the piece
	glm::vec3 color;      // RGB color for this tetromino
};

// Runtime state of a single piece on the board.
struct TetrisPiece
{
	TetrominoType Type = TetrominoType::Count;
	TetrisPieceState State = TetrisPieceState::Active;

	// Grid position (0-9 for columns, 0-19 for rows).
	glm::ivec2 Position = glm::ivec2(3, 0);

	// Rotation state (0-3, each is 90° clockwise).
	uint8_t Rotation = 0;

	// Color (RGBA, with alpha for visual effects).
	glm::vec4 Color = glm::vec4(1.0f);

	// Velocity for soft/hard drop.
	float DropSpeed = 1.0f;
};

// Board configuration.
struct TetrisBoardConfig
{
	uint32_t Width = 10;   // Columns
	uint32_t Height = 20;  // Rows
	uint32_t Depth = 1;    // Layers (3D depth)
};

// Game state.
struct TetrisGameState
{
	enum class State : uint8_t
	{
		Menu = 0,
		Playing,
		Paused,
		GameOver
	};

	State CurrentState = State::Menu;

	// Score and level.
	int32_t Score = 0;
	int32_t Level = 1;
	int32_t LinesCleared = 0;

	// Combo counter for multi-line clears.
	int32_t Combo = 0;

	// Current piece and next piece.
	TetrisPiece CurrentPiece;
	TetrisPiece NextPiece;

	// Game board data (flattened 3D array).
	std::vector<uint8_t> Board;  // 0 = empty, 1-7 = tetromino types + 1

	// Line clear animation state.
	std::vector<bool> LinesToClear;
	float ClearAnimationTime = 0.0f;

	// Timing.
	float PieceDropTimer = 0.0f;
	float PieceDropInterval = 0.5f;  // Seconds between auto-drops

	// Game over state.
	bool HasGameOver = false;
	float GameOverTimer = 0.0f;

	// Board dimensions.
	uint32_t Width = 10;
	uint32_t Height = 20;

	// Tetris definitions for all tetrominoes.
	static const TetrominoDefinition kTetrominoDefinitions[7];
	static const TetrominoDefinition& GetDefinition(TetrominoType type);
	static TetrominoDefinition CreateDefinition(TetrominoType type);

	// Initialize board data.
	void InitializeBoard();

	// Check if a position is valid.
	bool IsValidPosition(glm::ivec2 pos) const;

	// Check if a position is within bounds.
	bool IsWithinBounds(glm::ivec2 pos) const;

	// Get board index from grid position.
	uint32_t GetBoardIndex(glm::ivec2 pos) const;

	// Get grid position from board index.
	glm::ivec2 GetGridPosition(uint32_t index) const;

	// Start the game.
	void StartGame();

	// Get drop interval based on level.
	float GetDropInterval() const;

	// Update game state.
	void Update(float deltaTime);

	// Move piece horizontally.
	void MovePiece(TetrisDirection direction);

	// Rotate piece.
	void RotatePiece();

	// Hard drop.
	void HardDrop();
};

}
