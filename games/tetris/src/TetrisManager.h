#pragma once

#include "TetrisTypes.h"

namespace Tetris {

// The Tetris rules, with no rendering, no engine and no key bindings.
//
// Everything here is deterministic apart from piece selection, which is
// seeded from the given generator so a test can drive a known sequence.
class TetrisManager
{
public:
	TetrisManager() = default;

	// Prepare a fresh game. Must be called before StartGame().
	void Initialize();

	// Clear all progress and return to the menu.
	void Reset();

	// Begin a new game: empty board, zero score, two pieces spawned.
	void StartGame();

	// Pause if playing, resume if paused. Ignored otherwise.
	void TogglePause();

	// Advance gravity and held-key repeats by dt seconds.
	void Update(float deltaTime);

	// Apply a player action. Held actions (move, soft drop) repeat while
	// `pressed` is true; the rest act once per press.
	void HandleAction(Action action, bool pressed);

	const TetrisGameState& GetState() const { return m_State; }
	TetrisGameState& GetState() { return m_State; }

	// Tetromino definition by type.
	static const TetrominoDefinition& GetTetrominoDefinition(TetrominoType type);

	// A new piece of the given type, at the spawn position, rotation 0.
	TetrisPiece CreatePiece(TetrominoType type) const;

	// Rotate the active piece, kicking it off a wall or floor if the rotated
	// shape does not fit where it stands. Returns false when no kick works.
	bool RotatePiece(bool clockwise = true);

	// Move the active piece one column. Returns false if blocked.
	bool MovePiece(TetrisDirection direction);

	// Drop the active piece one row. Returns false if blocked.
	bool SoftDrop();

	// Drop the active piece to the floor and lock it.
	void HardDrop();

	// Where the active piece would come to rest, as a piece origin.
	glm::ivec2 GetGhostPosition() const;

	// True when the active piece, at its current rotation, fits at `pos`.
	bool CanMoveTo(glm::ivec2 pos) const;

	// True when the given piece type and rotation fit at `pos`.
	bool CanPlaceAt(glm::ivec2 pos, uint8_t rotation) const;

	// Write the active piece into the board, clear lines and spawn the next.
	void LockPiece();

	// Remove every full row, dropping the rows above it down. Updates score,
	// level and combo. Safe to call at any time.
	void ClearLines();

	// Score for clearing `lines` at `level` with `combo` consecutive clears.
	static int32_t GetLineScore(uint32_t lines, uint32_t level, uint32_t combo);

	// Seconds between auto-drops at the current level.
	float GetDropInterval() const;

private:
	// Advance the held horizontal / soft-drop repeats.
	void UpdateHeldActions(float deltaTime);

	// Spawn `m_State.NextPiece` as the active piece and roll a new next one.
	void SpawnNextPiece();

	// End the game if the newly spawned piece has nowhere to go.
	void CheckGameOver();

	// Pick the next piece, weighted by type.
	TetrominoType NextRandomType();

	TetrisGameState m_State;

	// Held actions, for the delayed-autorepeat repeats.
	bool m_MoveLeftHeld = false;
	bool m_MoveRightHeld = false;
	bool m_SoftDropHeld = false;

	// Seconds until the next horizontal / soft-drop repeat. A fresh press arms
	// these with the initial delay; the action itself fires immediately on the
	// press, so holding a key is not delayed by it.
	float m_MoveRepeatTimer = 0.0f;
	float m_SoftDropRepeatTimer = 0.0f;

	// Piece selection state.
	uint32_t m_RandomState = 0x9E3779B9u;

	// Offsets tried in order when a rotation does not fit in place.
	static constexpr int32_t kWallKickOffsets[][2] = {
		{ 0,  0 },
		{ -1,  0 },
		{ 1,  0 },
		{ -2,  0 },
		{ 2,  0 },
		{ 0, -1 },
		{ -1, -1 },
		{ 1, -1 },
	};

	// Seconds a key must be held before it starts repeating, and the interval
	// between repeats afterwards. The timers count down to the next repeat.
	static constexpr float kMoveDelay = 0.15f;
	static constexpr float kMoveRepeat = 0.05f;
	static constexpr float kSoftDropRepeat = 0.03f;

	// Ceiling on repeats applied in a single update, so one long frame cannot
	// move the piece across the board.
	static constexpr int kMaxRepeatsPerFrame = 4;
};

}  // namespace Tetris