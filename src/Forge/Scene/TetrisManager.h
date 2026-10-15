#pragma once

#include "TetrisTypes.h"
#include <vector>
#include <functional>

namespace Forge {

// Core Tetris game manager.
class TetrisManager
{
public:
	TetrisManager() = default;
	~TetrisManager() = default;

	// Initialize the game manager.
	void Initialize();

	// Reset the game to initial state.
	void Reset();

	// Update game state (called each frame).
	void Update(float deltaTime);

	// Handle input events.
	void HandleInput(int keyCode, bool pressed);

	// Start a new game.
	void StartGame();

	// Pause/resume the game.
	void TogglePause();

	// Get current game state.
	const TetrisGameState& GetState() const { return m_State; }
	TetrisGameState& GetState() { return m_State; }

	// Get tetromino definition by type.
	static const TetrominoDefinition& GetTetrominoDefinition(TetrominoType type);

	// Create a new tetromino piece of given type.
	TetrisPiece CreatePiece(TetrominoType type) const;

	// Rotate piece (with wall kick).
	void RotatePiece();

	// Move piece horizontally.
	void MovePiece(TetrisDirection direction);

	// Soft drop (faster fall).
	void SoftDrop();

	// Hard drop (instant fall).
	void HardDrop();

	// Get ghost piece position.
	glm::ivec2 GetGhostPosition() const;

	// Check if piece can move to given position.
	bool CanMoveTo(glm::ivec2 pos) const;

	// Lock current piece into the board.
	void LockPiece();

	// Clear completed lines.
	void ClearLines();

	// Get score for clearing N lines at given level.
	int32_t GetLineScore(uint32_t lines, uint32_t level, uint32_t combo) const;

	// Get drop interval based on level.
	float GetDropInterval() const;

	// Event callbacks.
	std::function<void()> OnPieceLock;
	std::function<void(uint32_t lines)> OnLinesCleared;
	std::function<void(int32_t score)> OnGameOver;

private:
	TetrisGameState m_State;

	// Input state.
	bool m_RotatePressed = false;
	bool m_LeftPressed = false;
	bool m_RightPressed = false;
	bool m_DownPressed = false;
	bool m_SpacePressed = false;

	// Input delay for repeated actions.
	float m_RotateDelay = 0.2f;
	float m_MoveDelay = 0.1f;
	float m_CurrentDelay = 0.0f;

	// Tetromino definitions.
	std::vector<TetrominoDefinition> m_TetrominoDefinitions;

	// Helper to get random tetromino type.
	TetrominoType GetRandomTetromino() const;

	// Rotate a piece's shape by 90° clockwise.
	glm::ivec2 RotateShape(const glm::ivec2& pos, uint8_t rotation) const;

	// Get shape positions for current rotation.
	std::vector<glm::ivec2> GetShapePositions(uint8_t rotation) const;

	// Check if piece can rotate (wall kick).
	bool CanRotate() const;

	// Apply wall kick on rotation.
	bool ApplyWallKick(int32_t offset);
};

}
