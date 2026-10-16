#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Tetris {

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
	Destroyed
};

// A player action, decoupled from any particular key binding.
//
// The rules below are pure arithmetic and know nothing about GLFW, the window
// or the engine: input arrives as one of these. That keeps the whole game
// testable with no window and no graphics device, which is the reason this
// enum exists rather than passing key codes around.
enum class Action : uint8_t
{
	None = 0,
	MoveLeft,
	MoveRight,
	SoftDrop,
	HardDrop,
	RotateCW,
	RotateCCW,
	TogglePause,
	Restart
};

// Tetris piece definition (shape + color).
struct TetrominoDefinition
{
	glm::ivec2 shape[4];  // 4 blocks of the piece, rotation 0
	glm::vec3 color;      // RGB color for this tetromino
};

// Runtime state of a single piece on the board.
struct TetrisPiece
{
	TetrominoType Type = TetrominoType::Count;
	TetrisPieceState State = TetrisPieceState::Active;

	// Grid position of the piece origin (0-9 for columns, 0-19 for rows).
	// The four blocks sit at origin + each rotated shape offset.
	glm::ivec2 Position = glm::ivec2(3, 0);

	// Rotation state (0-3, each is 90 degrees clockwise).
	uint8_t Rotation = 0;

	// Color, with alpha for visual effects.
	glm::vec4 Color = glm::vec4(1.0f);

	// Blocks for the current rotation, in world-independent grid offsets.
	glm::ivec2 Blocks[4] = {};
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

	// Game board data, row-major. 0 = empty, 1-7 = tetromino type + 1.
	std::vector<uint8_t> Board;

	// Timing.
	float PieceDropTimer = 0.0f;
	float PieceDropInterval = 0.8f;  // Seconds between auto-drops

	// Game over state.
	bool HasGameOver = false;

	// Board dimensions.
	uint32_t Width = 10;
	uint32_t Height = 20;

	// Tetromino definitions for all tetrominoes.
	static const TetrominoDefinition kTetrominoDefinitions[7];
	static const TetrominoDefinition& GetDefinition(TetrominoType type);

	// Initialize board data.
	void InitializeBoard();

	// Check if a grid cell is inside the board.
	bool IsValidPosition(glm::ivec2 pos) const;

	// Get board index from grid position.
	uint32_t GetBoardIndex(glm::ivec2 pos) const;

	// Get grid position from board index.
	glm::ivec2 GetGridPosition(uint32_t index) const;

	// Fill a piece's Blocks from its type and rotation.
	void UpdatePieceBlocks(TetrisPiece& piece) const;

	// True when every cell of the row is occupied.
	bool IsRowFull(uint32_t row) const;
};

// The four grid offsets of `type` rotated by `rotation` quarter-turns
// clockwise. Always non-negative, so a piece can never sit half outside the
// board because of where its rotation origin happens to fall.
void GetRotatedBlocks(TetrominoType type, uint8_t rotation, glm::ivec2 outBlocks[4]);

// Human-readable name of the game state, for logging and the display component.
const char* ToString(TetrisGameState::State state);

}  // namespace Tetris