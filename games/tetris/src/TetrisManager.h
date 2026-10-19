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

	// The colour a fire block is drawn in. A fire block is a normal tetromino
	// flagged to melt what it touches, so it has no shape of its own - the
	// colour is what tells the player a falling piece is fire.
	static const glm::vec3 kFireColor;

	// The current chance (0..1) that the next fire roll produces a fire block.
	// This is what the fire meter shows.
	float GetFireChance() const { return m_FireChance; }

	// Raise the fire chance (capped at certain). Screen shake feeds the fire
	// meter: a harder landing, or a bigger line clear, makes a fire block more
	// likely on the next roll.
	void AddFireChance(float amount);

	// A new piece of the given type, at the spawn position, rotation 0.
	TetrisPiece CreatePiece(TetrominoType type) const;

	// Rotate the active piece, kicking it off a wall or floor if the rotated
	// shape does not fit where it stands. Returns false when no kick works.
	bool RotatePiece(bool clockwise = true);

	// Move the active piece one column. Returns false if blocked.
	bool MovePiece(TetrisDirection direction);

	// Drop the active piece one row. Returns false if blocked.
	bool SoftDrop();

	// Drop the active piece to the floor, pack the stack beneath it and lock it.
	void HardDrop();

	// Where the active piece would come to rest, as a piece origin.
	glm::ivec2 GetGhostPosition() const;

	// True when the active piece, at its current rotation, fits at `pos`.
	bool CanMoveTo(glm::ivec2 pos) const;

	// True when the given piece type and rotation fit at `pos`.
	bool CanPlaceAt(glm::ivec2 pos, uint8_t rotation) const;

	// Write the active piece into the board, clear lines and spawn the next.
	// `compact` packs the stack first, which is what a hard drop does; a
	// gravity lock leaves the stack where it stands.
	void LockPiece(bool compact = false);

	// Slide every block down onto the first occupied cell beneath it, closing
	// every gap in every column. The bottom of the stack is packed, the order
	// of each column is kept, and the top is left empty. Records what moved in
	// `CompactedCells` for the view to animate, and returns how many blocks
	// moved (0 when the stack was already packed).
	uint32_t CompactStack();

	// The hard drop's pack. The piece falls to the floor, and the columns it
	// lands on close the gap under it: the blocks standing on that gap - and
	// the blocks standing on them, all the way up - pack down, while the
	// blocks under the gap keep their floor. A column the piece never touched
	// keeps its holes, and a gap opening more than three rows below the row the
	// piece landed on stays open. The piece then travels down as one body, all
	// four blocks moving the same number of rows, as far as all four of them
	// fit, so it arrives in the shape it was dropped in.
	uint32_t CompactStack(const TetrisPiece& settled);

	// Remove every full row, dropping the rows above it down. Updates score,
	// level and combo. Safe to call at any time.
	void ClearLines();

	// Score for clearing `lines` at `level`, scaled by the live `multiplier`
	// and the `comboMultiplier` for consecutive clears.
	static int32_t GetLineScore(uint32_t lines, uint32_t level, uint32_t multiplier, uint32_t comboMultiplier);

	// The reward label shown for clearing 1-4 lines.
	static const char* RewardLabelForLines(int32_t lines);

	// The multiplier tier a clear of `lines` lines adds to the stack:
	// 1 -> x2, 2 -> x3, 3 -> x4, 4 -> x4.
	static uint32_t MultiplierTierForLines(uint32_t lines);

	// Seconds between auto-drops at the current level.
	float GetDropInterval() const;

private:
	// Advance the held horizontal / soft-drop repeats.
	void UpdateHeldActions(float deltaTime);

	// Spawn `m_State.NextPiece` as the active piece and roll a new next one.
	void SpawnNextPiece();

	// End the game if the newly spawned piece has nowhere to go.
	void CheckGameOver();

	// Pick the next piece, weighted by type. `lastType` is the piece just
	// spawned; a repeat of it is rejected half the time, so the same piece
	// appearing back-to-back is half as likely as the raw weights allow.
	TetrominoType NextRandomType(TetrominoType lastType);

	// Decide whether the piece being spawned is a fire block. The roll runs on
	// every spawn: at the current chance it is a fire block, and the chance
	// resets to kFireBaseChance; every spawn that is not a fire block raises
	// the next spawn's chance by kFireChanceStep, so the longer the drought the
	// more certain the next fire block.
	bool RollFireSpawn();

	// One falling step of a fire block. Every block that would move into an
	// occupied cell melts that block and burns itself away; the blocks that
	// keep clear space keep falling. Returns false when the piece is fully
	// consumed (all blocks burned, or all settled at the floor with nothing
	// left to burn), in which case the next piece has been spawned.
	bool FireDrop();

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

	// Fire-block spawning state: the chance that the next spawn is a fire
	// block. It starts at the base, climbs 0.2% every spawn it misses, and
	// resets to the base after a fire block.
	float m_FireChance = 0.0f;

	// The roll runs on every spawn, from a base of 0%: a fire block is only
	// offered once screen shake (or a run of misses) has charged the meter,
	// and each miss makes the next one likelier.
	static constexpr float kFireBaseChance = 0.0f;

	// Ten times slower than the ladder this started at: the meter climbs
	// 0.2% per missed spawn, so a fire block stays the rare event it is and
	// the meter has to be charged by a hard landing to reach anything.
	static constexpr float kFireChanceStep = 0.002f;
	static constexpr float kFireMaxChance = 1.0f;

	// A hard drop's compaction reaches this many rows below the row it landed
	// on. A gap opening deeper than that is not the landing's business: closing
	// it would move blocks the drop never touched. Blocks above the gap are
	// moved by the landing because the block they stand on is moved by it.
	static constexpr uint32_t kCompactionReachRows = 3u;

	// A clear's tier stacks onto the live multiplier and stays live for this
	// many turns; the combo doubles per consecutive clear. Both are capped so a
	// long streak cannot overflow the score.
	static constexpr int32_t kMultiplierTurns = 5;
	static constexpr uint32_t kMaxMultiplier = 256u;
	static constexpr uint32_t kMaxComboMultiplier = 64u;

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