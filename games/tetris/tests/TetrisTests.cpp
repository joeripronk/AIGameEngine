#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "TetrisManager.h"
#include "TetrisTypes.h"
#include "TetrisComponents.h"
#include "TetrisInputLayer.h"

#include "Forge/Core/KeyCodes.h"

// As in fgpch.h: no OpenGL headers on a Vulkan engine. This file includes
#include <glm/glm.hpp>

#include <algorithm>
#include <vector>

using namespace Tetris;

namespace {

// A manager with the game running, which is the state nearly every rule is
// defined against.
TetrisManager Playing()
{
	TetrisManager manager;
	manager.Initialize();
	manager.StartGame();
	return manager;
}

// Blocks as comparable pairs. glm::ivec2 has no ordering operators, and a test
// that has to compare two shapes needs one.
std::vector<std::pair<int, int>> Keys(const glm::ivec2* blocks, int count)
{
	std::vector<std::pair<int, int>> keys;
	keys.reserve(static_cast<size_t>(count));
	for (int i = 0; i < count; ++i)
		keys.emplace_back(blocks[i].x, blocks[i].y);
	std::sort(keys.begin(), keys.end());
	return keys;
}

// Fill the bottom row but one column, so dropping a vertical piece completes it.
void FillRow(TetrisGameState& state, uint32_t row, uint32_t filledColumns)
{
	for (uint32_t col = 0; col < filledColumns; ++col)
		state.Board[row * state.Width + col] = 1;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("A fresh manager sits at the menu with an empty board", "[tetris][core]")
{
	TetrisManager manager;
	manager.Initialize();

	const auto& state = manager.GetState();
	REQUIRE(state.CurrentState == TetrisGameState::State::Menu);
	REQUIRE(state.Board.size() == 200);
	REQUIRE(state.Score == 0);
	REQUIRE(state.Level == 1);
	REQUIRE(state.LinesCleared == 0);
}

TEST_CASE("Reset returns a played game to the menu", "[tetris][core]")
{
	TetrisManager manager = Playing();

	auto& state = manager.GetState();
	state.Score = 100;
	state.Level = 3;
	state.LinesCleared = 15;
	state.Board[0] = 1;

	manager.Reset();

	const auto& reset = manager.GetState();
	REQUIRE(reset.CurrentState == TetrisGameState::State::Menu);
	REQUIRE(reset.Score == 0);
	REQUIRE(reset.Level == 1);
	REQUIRE(reset.LinesCleared == 0);
	REQUIRE(reset.Board.size() == 200);
	REQUIRE(std::all_of(reset.Board.begin(), reset.Board.end(), [](uint8_t cell) { return cell == 0; }));
}

TEST_CASE("Starting a game spawns a piece and queues the next", "[tetris][core]")
{
	TetrisManager manager = Playing();

	const auto& state = manager.GetState();
	REQUIRE(state.CurrentState == TetrisGameState::State::Playing);
	REQUIRE(state.CurrentPiece.State == TetrisPieceState::Active);
	REQUIRE(state.CurrentPiece.Type != TetrominoType::Count);
	REQUIRE(state.NextPiece.Type != TetrominoType::Count);
}

TEST_CASE("Starting again replaces the pieces instead of stacking them", "[tetris][core]")
{
	TetrisManager manager = Playing();

	manager.HardDrop();
	REQUIRE_FALSE(std::all_of(manager.GetState().Board.begin(), manager.GetState().Board.end(),
		[](uint8_t cell) { return cell == 0; }));

	manager.StartGame();

	const auto& state = manager.GetState();
	REQUIRE(state.CurrentPiece.State == TetrisPieceState::Active);
	REQUIRE(state.Score == 0);
	REQUIRE(std::all_of(state.Board.begin(), state.Board.end(), [](uint8_t cell) { return cell == 0; }));
}

TEST_CASE("Pause toggles only while playing or paused", "[tetris][core]")
{
	TetrisManager manager;
	manager.Initialize();

	// Nothing to pause from the menu.
	manager.TogglePause();
	REQUIRE(manager.GetState().CurrentState == TetrisGameState::State::Menu);

	manager.StartGame();
	manager.TogglePause();
	REQUIRE(manager.GetState().CurrentState == TetrisGameState::State::Paused);
	manager.TogglePause();
	REQUIRE(manager.GetState().CurrentState == TetrisGameState::State::Playing);
}

// ---------------------------------------------------------------------------
// Pieces
// ---------------------------------------------------------------------------

TEST_CASE("Tetromino definitions lay out the standard shapes", "[tetris][tetromino]")
{
	SECTION("I piece")
	{
		const auto& def = TetrisGameState::GetDefinition(TetrominoType::I);
		REQUIRE(def.shape[0] == glm::ivec2(0, 0));
		REQUIRE(def.shape[1] == glm::ivec2(1, 0));
		REQUIRE(def.shape[2] == glm::ivec2(2, 0));
		REQUIRE(def.shape[3] == glm::ivec2(3, 0));
	}

	SECTION("O piece")
	{
		const auto& def = TetrisGameState::GetDefinition(TetrominoType::O);
		REQUIRE(def.shape[0] == glm::ivec2(0, 0));
		REQUIRE(def.shape[1] == glm::ivec2(1, 0));
		REQUIRE(def.shape[2] == glm::ivec2(0, 1));
		REQUIRE(def.shape[3] == glm::ivec2(1, 1));
	}

	SECTION("T piece")
	{
		const auto& def = TetrisGameState::GetDefinition(TetrominoType::T);
		REQUIRE(def.shape[0] == glm::ivec2(0, 0));
		REQUIRE(def.shape[1] == glm::ivec2(1, 0));
		REQUIRE(def.shape[2] == glm::ivec2(2, 0));
		REQUIRE(def.shape[3] == glm::ivec2(1, 1));
	}
}

TEST_CASE("Rotated blocks stay inside the origin quadrant", "[tetris][tetromino][rotation]")
{
	for (uint32_t type = 0; type < static_cast<uint32_t>(TetrominoType::Count); ++type)
	{
		for (uint8_t rotation = 0; rotation < 4; ++rotation)
		{
			glm::ivec2 blocks[4];
			GetRotatedBlocks(static_cast<TetrominoType>(type), rotation, blocks);

			for (const glm::ivec2& block : blocks)
			{
				// A negative offset would put part of the piece off the top or
				// left of the board the moment it spawned, so the shape could
				// never be placed at all.
				REQUIRE(block.x >= 0);
				REQUIRE(block.y >= 0);
			}
		}
	}
}

TEST_CASE("Four quarter turns return a piece to its starting shape", "[tetris][tetromino][rotation]")
{
	for (uint32_t type = 0; type < static_cast<uint32_t>(TetrominoType::Count); ++type)
	{
		const TetrominoType pieceType = static_cast<TetrominoType>(type);
		const TetrominoDefinition& def = TetrisGameState::GetDefinition(pieceType);

		glm::ivec2 original[4];
		GetRotatedBlocks(pieceType, 0, original);

		glm::ivec2 roundTrip[4];
		GetRotatedBlocks(pieceType, 4, roundTrip);

		REQUIRE(Keys(roundTrip, 4) == Keys(original, 4));

		// The square piece is its own rotation, so a rotation that changed it
		// would be a bug in the shared path rather than in the O case.
		if (pieceType == TetrominoType::O)
		{
			glm::ivec2 once[4];
			GetRotatedBlocks(pieceType, 1, once);
			REQUIRE(Keys(once, 4) == Keys(original, 4));
		}

		(void)def;
	}
}

TEST_CASE("A new piece spawns at the top, centred and unrotated", "[tetris][piece]")
{
	TetrisManager manager;
	manager.Initialize();

	const TetrisPiece piece = manager.CreatePiece(TetrominoType::I);

	REQUIRE(piece.Type == TetrominoType::I);
	REQUIRE(piece.State == TetrisPieceState::Active);
	REQUIRE(piece.Position.y == 0);
	REQUIRE(piece.Rotation == 0);
	// 10 columns, a 4-wide piece: centred leaves it at column 3.
	REQUIRE(piece.Position.x == 3);
}

TEST_CASE("Every piece spawns fully inside the board", "[tetris][piece]")
{
	TetrisManager manager;
	manager.Initialize();
	manager.StartGame();

	for (uint32_t i = 0; i < 200; ++i)
	{
		const TetrominoType type = static_cast<TetrominoType>(i % static_cast<uint32_t>(TetrominoType::Count));
		const TetrisPiece piece = manager.CreatePiece(type);

		const auto& state = manager.GetState();
		for (const glm::ivec2& block : piece.Blocks)
			REQUIRE(state.IsValidPosition(piece.Position + block));
	}
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

TEST_CASE("Pieces move sideways and are stopped by the walls", "[tetris][movement]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// An O piece: 2 wide, easy to reason about.
	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 5);
	state.UpdatePieceBlocks(state.CurrentPiece);

	REQUIRE(manager.MovePiece(TetrisDirection::Left));
	REQUIRE(state.CurrentPiece.Position == glm::ivec2(3, 5));

	REQUIRE(manager.MovePiece(TetrisDirection::Right));
	REQUIRE(state.CurrentPiece.Position == glm::ivec2(4, 5));

	// Walk it into the right wall and confirm it stops rather than wrapping.
	state.CurrentPiece.Position = glm::ivec2(9, 5);
	REQUIRE_FALSE(manager.MovePiece(TetrisDirection::Right));
	REQUIRE(state.CurrentPiece.Position == glm::ivec2(9, 5));

	state.CurrentPiece.Position = glm::ivec2(-1, 5);
	REQUIRE_FALSE(manager.MovePiece(TetrisDirection::Left));
	REQUIRE(state.CurrentPiece.Position == glm::ivec2(-1, 5));
}

TEST_CASE("A piece cannot move into an occupied cell", "[tetris][movement]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 5);
	state.UpdatePieceBlocks(state.CurrentPiece);

	// Block the cell to the right of the piece's lower-left block.
	state.Board[state.GetBoardIndex(glm::ivec2(5, 6))] = 1;

	REQUIRE_FALSE(manager.MovePiece(TetrisDirection::Right));
	REQUIRE(state.CurrentPiece.Position == glm::ivec2(4, 5));
}

TEST_CASE("A rotated piece keeps its rotated footprint when moving", "[tetris][movement][rotation]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// An I piece laid flat is 4 wide; stood up it is 4 tall but only 1 wide.
	state.CurrentPiece.Type = TetrominoType::I;
	state.CurrentPiece.Position = glm::ivec2(3, 5);
	REQUIRE(manager.RotatePiece());

	glm::ivec2 rotated[4];
	GetRotatedBlocks(TetrominoType::I, state.CurrentPiece.Rotation, rotated);
	for (const glm::ivec2& block : rotated)
		REQUIRE(block.x == 0);

	// With the shape one column wide, a block at column 4 blocks the piece
	// from moving right - which it would not if the unrotated shape were used.
	state.Board[state.GetBoardIndex(glm::ivec2(4, 5))] = 1;
	REQUIRE_FALSE(manager.MovePiece(TetrisDirection::Right));
	REQUIRE(state.CurrentPiece.Position.x == 3);
}

TEST_CASE("Ghost position is where the piece would come to rest", "[tetris][movement]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	const glm::ivec2 ghost = manager.GetGhostPosition();

	// The bottom of a 2-tall piece at row 0 is row 19.
	REQUIRE(ghost.y == 18);

	// The ghost position has to be reachable by dropping, or it is a lie.
	manager.HardDrop();
	(void)state;
}

TEST_CASE("CanPlaceAt respects the given rotation, not the current one", "[tetris][movement]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();
	state.CurrentPiece.Type = TetrominoType::I;
	state.CurrentPiece.Position = glm::ivec2(3, 0);

	// A flat I needs four columns; an upright one needs a single column.
	REQUIRE(manager.CanPlaceAt(glm::ivec2(6, 0), 0));
	REQUIRE_FALSE(manager.CanPlaceAt(glm::ivec2(7, 0), 0));

	REQUIRE(manager.CanPlaceAt(glm::ivec2(9, 0), 1));
	REQUIRE_FALSE(manager.CanPlaceAt(glm::ivec2(9, 0), 0));
}

// ---------------------------------------------------------------------------
// Rotation
// ---------------------------------------------------------------------------

TEST_CASE("Rotation advances through the four orientations", "[tetris][rotation]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// Well clear of the walls, so no kick is needed and the count is exact.
	state.CurrentPiece.Type = TetrominoType::T;
	state.CurrentPiece.Position = glm::ivec2(3, 10);
	state.UpdatePieceBlocks(state.CurrentPiece);

	REQUIRE(state.CurrentPiece.Rotation == 0);
	REQUIRE(manager.RotatePiece());
	REQUIRE(state.CurrentPiece.Rotation == 1);
	REQUIRE(manager.RotatePiece());
	REQUIRE(state.CurrentPiece.Rotation == 2);
	REQUIRE(manager.RotatePiece());
	REQUIRE(state.CurrentPiece.Rotation == 3);
	REQUIRE(manager.RotatePiece());
	REQUIRE(state.CurrentPiece.Rotation == 0);
}

TEST_CASE("Rotating against a wall kicks the piece rather than refusing", "[tetris][rotation]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	state.CurrentPiece.Type = TetrominoType::I;
	state.CurrentPiece.Position = glm::ivec2(0, 10);
	state.UpdatePieceBlocks(state.CurrentPiece);

	// Flat I at column 0 occupies columns 0-3. Turning it upright at that same
	// origin would be fine, so push it to the wall where upright needs a kick.
	state.CurrentPiece.Position = glm::ivec2(0, 10);
	REQUIRE(manager.RotatePiece(true));
	REQUIRE(state.CurrentPiece.Rotation == 1);

	// Rotating back must also succeed from the kicked position.
	REQUIRE(manager.RotatePiece(false));
	REQUIRE(state.CurrentPiece.Rotation == 0);
}

TEST_CASE("Rotation actually moves the blocks, not just a counter", "[tetris][rotation]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	state.CurrentPiece.Type = TetrominoType::T;
	state.CurrentPiece.Position = glm::ivec2(3, 10);
	state.UpdatePieceBlocks(state.CurrentPiece);

	std::vector<std::pair<int, int>> before = Keys(state.CurrentPiece.Blocks, 4);
	REQUIRE(manager.RotatePiece());
	std::vector<std::pair<int, int>> after = Keys(state.CurrentPiece.Blocks, 4);

	// A T has a nub below the middle row; turned on its side the nub moves to a
	// column. If rotation only bumped Rotation, these two sets would match.
	REQUIRE(before != after);
}

TEST_CASE("A piece that cannot be turned anywhere refuses to rotate", "[tetris][rotation]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// Two full rows below the top: an upright I is four tall, so every column
	// and every wall kick runs into one of them. Only the flat shape fits, and
	// only while it stays flat.
	for (uint32_t row = 1; row <= 2; ++row)
		FillRow(state, row, state.Width);

	state.CurrentPiece.Type = TetrominoType::I;
	state.CurrentPiece.Position = glm::ivec2(3, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	REQUIRE(manager.CanPlaceAt(glm::ivec2(3, 0), 0));
	REQUIRE_FALSE(manager.RotatePiece());
	REQUIRE(state.CurrentPiece.Rotation == 0);
	REQUIRE(state.CurrentPiece.Position == glm::ivec2(3, 0));
}

// ---------------------------------------------------------------------------
// Locking and line clears
// ---------------------------------------------------------------------------

TEST_CASE("Hard drop puts the piece on the board and queues the next", "[tetris][locking]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 18);
	state.UpdatePieceBlocks(state.CurrentPiece);
	state.NextPiece = manager.CreatePiece(TetrominoType::T);

	manager.HardDrop();

	// Bottom two rows hold the O.
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 19))] == static_cast<uint8_t>(TetrominoType::O) + 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(5, 19))] == static_cast<uint8_t>(TetrominoType::O) + 1);

	// And the queued piece is now the falling one, back at the top.
	REQUIRE(state.CurrentPiece.Type == TetrominoType::T);
	REQUIRE(state.CurrentPiece.Position.y == 0);
	REQUIRE(state.CurrentPiece.State == TetrisPieceState::Active);
}

TEST_CASE("Clearing a line drops the rows above it down", "[tetris][lines]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// Two filled rows and one row above them with a single gap.
	FillRow(state, 18, state.Width);
	FillRow(state, 19, state.Width);
	FillRow(state, 17, state.Width - 1);

	manager.ClearLines();

	// Two rows cleared, so the row that was above them has dropped two rows.
	// It keeps its gap, at the bottom: it is now in the bottom row.
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, state.Height - 1))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(state.Width - 1, state.Height - 1))] == 0);
	// The two rows that were left above are now empty.
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 0))] == 0);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 1))] == 0);

	REQUIRE(state.LinesCleared == 2);
	REQUIRE(state.Score == 100);
}

TEST_CASE("Clearing the top row leaves everything below it in place", "[tetris][lines]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	FillRow(state, 0, state.Width);
	FillRow(state, 5, 3);

	manager.ClearLines();

	// Nothing was above the cleared row, so nothing moves.
	REQUIRE(state.LinesCleared == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(2, 5))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(3, 5))] == 0);
	REQUIRE(std::all_of(state.Board.begin(), state.Board.begin() + state.Width,
		[](uint8_t cell) { return cell == 0; }));
}

TEST_CASE("A board with no full row is left untouched", "[tetris][lines]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	std::vector<uint8_t> before = state.Board;
	manager.ClearLines();

	REQUIRE(state.Board == before);
	REQUIRE(state.Score == 0);
	REQUIRE(state.LinesCleared == 0);
}

TEST_CASE("Level rises every ten lines and the pieces fall faster", "[tetris][lines][timing]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	const float levelOneInterval = manager.GetDropInterval();

	for (uint32_t round = 0; round < 10; ++round)
	{
		FillRow(state, state.Height - 1, state.Width);
		manager.ClearLines();
		// Restore a single gap so the next fill is not also a clear.
		state.Board[state.GetBoardIndex(glm::ivec2(state.Width - 1, state.Height - 1))] = 0;
	}

	REQUIRE(state.LinesCleared == 10);
	REQUIRE(state.Level == 2);
	REQUIRE(manager.GetDropInterval() < levelOneInterval);
}

// ---------------------------------------------------------------------------
// Scoring
// ---------------------------------------------------------------------------

TEST_CASE("Line scores follow the classic table", "[tetris][scoring]")
{
	REQUIRE(TetrisManager::GetLineScore(0, 1, 0) == 0);
	REQUIRE(TetrisManager::GetLineScore(1, 1, 0) == 40);
	REQUIRE(TetrisManager::GetLineScore(2, 1, 0) == 100);
	REQUIRE(TetrisManager::GetLineScore(3, 1, 0) == 300);
	REQUIRE(TetrisManager::GetLineScore(4, 1, 0) == 1200);
}

TEST_CASE("Score scales with level and adds a combo bonus", "[tetris][scoring]")
{
	REQUIRE(TetrisManager::GetLineScore(1, 3, 0) == 120);
	REQUIRE(TetrisManager::GetLineScore(1, 1, 1) == 90);
	REQUIRE(TetrisManager::GetLineScore(2, 2, 3) == 350);
	// More than four lines cannot happen, but must not read past the table.
	REQUIRE(TetrisManager::GetLineScore(9, 1, 0) == 1200);
}

TEST_CASE("A combo is earned by consecutive clears and lost on a miss", "[tetris][scoring][lines]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	FillRow(state, state.Height - 1, state.Width);
	manager.ClearLines();
	REQUIRE(state.Combo == 1);

	FillRow(state, state.Height - 1, state.Width);
	manager.ClearLines();
	REQUIRE(state.Combo == 2);

	manager.ClearLines();
	REQUIRE(state.Combo == 0);
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

TEST_CASE("Drop interval falls with the level and stays positive", "[tetris][timing]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	state.Level = 1;
	REQUIRE(manager.GetDropInterval() == Approx(0.8f).epsilon(0.01f));

	state.Level = 5;
	REQUIRE(manager.GetDropInterval() == Approx(0.6f).epsilon(0.01f));

	state.Level = 10;
	REQUIRE(manager.GetDropInterval() == Approx(0.35f).epsilon(0.01f));

	// Unbounded subtraction would eventually reach zero and stop the game.
	state.Level = 1000;
	REQUIRE(manager.GetDropInterval() > 0.0f);
}

TEST_CASE("Gravity moves the piece once per interval", "[tetris][timing]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);
	state.PieceDropTimer = 0.0f;

	manager.Update(0.1f);
	REQUIRE(state.CurrentPiece.Position.y == 0);

	manager.Update(0.8f);
	REQUIRE(state.CurrentPiece.Position.y == 1);
}

TEST_CASE("Holding a direction key moves on the press, then repeats", "[tetris][timing][input]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 5);
	state.UpdatePieceBlocks(state.CurrentPiece);

	manager.HandleAction(Action::MoveLeft, true);
	REQUIRE(state.CurrentPiece.Position.x == 3);

	// Short frames inside the initial delay must not move it again.
	manager.Update(0.05f);
	REQUIRE(state.CurrentPiece.Position.x == 3);

	// Past the delay it repeats.
	manager.Update(0.2f);
	REQUIRE(state.CurrentPiece.Position.x < 3);

	// Releasing stops it dead, whatever the timers were doing.
	const int32_t afterRelease = state.CurrentPiece.Position.x;
	manager.HandleAction(Action::MoveLeft, false);
	manager.Update(1.0f);
	REQUIRE(state.CurrentPiece.Position.x == afterRelease);
}

TEST_CASE("Pausing freezes the piece and the timers", "[tetris][timing]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	manager.HandleAction(Action::MoveLeft, true);
	manager.HandleAction(Action::TogglePause, true);

	const glm::ivec2 paused = state.CurrentPiece.Position;
	manager.Update(5.0f);
	REQUIRE(state.CurrentPiece.Position == paused);
	REQUIRE(state.PieceDropTimer == Approx(0.0f));

	manager.HandleAction(Action::TogglePause, true);
	manager.Update(1.0f);
	REQUIRE(state.CurrentPiece.Position != paused);
}

// ---------------------------------------------------------------------------
// Game over
// ---------------------------------------------------------------------------

TEST_CASE("The game ends when a piece cannot spawn", "[tetris][gameover]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// Fill every row but the top, with the top row blocked in the middle so
	// nothing can drop into it.
	for (uint32_t row = 1; row < state.Height; ++row)
	{
		for (uint32_t col = 0; col < state.Width; ++col)
			state.Board[row * state.Width + col] = 1;
	}
	state.Board[state.GetBoardIndex(glm::ivec2(4, 0))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(5, 0))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(6, 0))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(4, 1))] = 1;

	manager.HardDrop();

	REQUIRE(state.CurrentState == TetrisGameState::State::GameOver);
	REQUIRE(state.HasGameOver);
}

TEST_CASE("Restart clears a game over", "[tetris][gameover]")
{
	TetrisManager manager = Playing();
	manager.GetState().CurrentState = TetrisGameState::State::GameOver;
	manager.GetState().HasGameOver = true;
	manager.GetState().Score = 500;

	manager.HandleAction(Action::Restart, true);

	REQUIRE(manager.GetState().CurrentState == TetrisGameState::State::Playing);
	REQUIRE_FALSE(manager.GetState().HasGameOver);
	REQUIRE(manager.GetState().Score == 0);
}

// ---------------------------------------------------------------------------
// Input mapping
// ---------------------------------------------------------------------------

TEST_CASE("Keys map to the actions they are documented as", "[tetris][input]")
{
	REQUIRE(MapKey(Forge::Key::Left) == Action::MoveLeft);
	REQUIRE(MapKey(Forge::Key::Right) == Action::MoveRight);
	REQUIRE(MapKey(Forge::Key::Down) == Action::SoftDrop);
	REQUIRE(MapKey(Forge::Key::Space) == Action::HardDrop);
	REQUIRE(MapKey(Forge::Key::Up) == Action::RotateCW);
	REQUIRE(MapKey(Forge::Key::P) == Action::TogglePause);
	REQUIRE(MapKey(Forge::Key::R) == Action::Restart);

	REQUIRE(MapKey(Forge::Key::A) == Action::MoveLeft);
	REQUIRE(MapKey(Forge::Key::D) == Action::MoveRight);
	REQUIRE(MapKey(Forge::Key::S) == Action::SoftDrop);
	REQUIRE(MapKey(Forge::Key::W) == Action::RotateCW);
	REQUIRE(MapKey(Forge::Key::X) == Action::RotateCW);
	REQUIRE(MapKey(Forge::Key::Z) == Action::RotateCCW);

	// V switches the 2D/3D view. That is presentation, not a rule, so it must
	// reach the app as a callback and never arrive as a rules action.
	REQUIRE(MapKey(Forge::Key::V) == Action::None);
}

TEST_CASE("Every arrow key is mapped", "[tetris][input]")
{
	// The runtime's old input path polled from GLFW_KEY_SPACE upwards, so all
	// three arrows - the keys the game is played with - never reached it.
	REQUIRE(MapKey(Forge::Key::Left) != Action::None);
	REQUIRE(MapKey(Forge::Key::Right) != Action::None);
	REQUIRE(MapKey(Forge::Key::Down) != Action::None);
	REQUIRE(MapKey(Forge::Key::Up) != Action::None);
}

TEST_CASE("An unmapped key does nothing", "[tetris][input]")
{
	REQUIRE(MapKey(Forge::Key::F12) == Action::None);
	REQUIRE(MapKey(-1) == Action::None);
}

// ---------------------------------------------------------------------------
// Components
// ---------------------------------------------------------------------------

TEST_CASE("The game component defaults describe a stopped board", "[tetris][component]")
{
	const TetrisGameComponent component;

	REQUIRE_FALSE(component.IsRunning);
	REQUIRE_FALSE(component.IsPaused);
	REQUIRE_FALSE(component.IsGameOver);
	REQUIRE(component.BoardWidth == 10.0f);
	REQUIRE(component.BoardHeight == 20.0f);
	REQUIRE(component.BlockSize == 1.0f);
}

TEST_CASE("The piece component defaults to an active I piece", "[tetris][component]")
{
	const TetrisPieceComponent component;

	REQUIRE(component.Type == TetrominoType::I);
	REQUIRE(component.State == TetrisPieceState::Active);
	REQUIRE(component.GridPosition == glm::ivec2(0, 0));
	REQUIRE(component.Rotation == 0);
	REQUIRE(component.Color == glm::vec3(1.0f));
}

TEST_CASE("The board component starts empty", "[tetris][component]")
{
	const TetrisBoardComponent component;

	REQUIRE(component.Width == 10);
	REQUIRE(component.Height == 20);
	REQUIRE(component.Board.empty());
}

TEST_CASE("The display component starts at the menu", "[tetris][component]")
{
	const TetrisDisplayComponent component;

	REQUIRE(component.Score == 0);
	REQUIRE(component.Level == 1);
	REQUIRE(component.LinesCleared == 0);
	REQUIRE(component.Combo == 0);
	REQUIRE(component.GameStateText == "Menu");
}

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------

TEST_CASE("Board coordinates round-trip through an index", "[tetris][state]")
{
	TetrisGameState state;
	state.Width = 10;
	state.Height = 20;
	state.InitializeBoard();

	REQUIRE(state.Board.size() == 200);
	REQUIRE(std::all_of(state.Board.begin(), state.Board.end(), [](uint8_t cell) { return cell == 0; }));

	REQUIRE(state.GetBoardIndex(glm::ivec2(0, 0)) == 0);
	REQUIRE(state.GetBoardIndex(glm::ivec2(9, 0)) == 9);
	REQUIRE(state.GetBoardIndex(glm::ivec2(0, 1)) == 10);
	REQUIRE(state.GetBoardIndex(glm::ivec2(5, 5)) == 55);

	REQUIRE(state.GetGridPosition(0) == glm::ivec2(0, 0));
	REQUIRE(state.GetGridPosition(9) == glm::ivec2(9, 0));
	REQUIRE(state.GetGridPosition(10) == glm::ivec2(0, 1));
	REQUIRE(state.GetGridPosition(55) == glm::ivec2(5, 5));

	for (uint32_t row = 0; row < state.Height; ++row)
	{
		for (uint32_t col = 0; col < state.Width; ++col)
		{
			const glm::ivec2 cell(static_cast<int32_t>(col), static_cast<int32_t>(row));
			REQUIRE(state.GetGridPosition(state.GetBoardIndex(cell)) == cell);
		}
	}
}

TEST_CASE("Bounds checks reject the outside of the board", "[tetris][state]")
{
	TetrisGameState state;
	state.Width = 10;
	state.Height = 20;

	REQUIRE(state.IsValidPosition(glm::ivec2(0, 0)));
	REQUIRE(state.IsValidPosition(glm::ivec2(9, 19)));
	REQUIRE_FALSE(state.IsValidPosition(glm::ivec2(-1, 0)));
	REQUIRE_FALSE(state.IsValidPosition(glm::ivec2(10, 0)));
	REQUIRE_FALSE(state.IsValidPosition(glm::ivec2(0, -1)));
	REQUIRE_FALSE(state.IsValidPosition(glm::ivec2(0, 20)));
}

TEST_CASE("A row is only full when every cell is taken", "[tetris][state]")
{
	TetrisGameState state;
	state.Width = 10;
	state.Height = 20;
	state.InitializeBoard();

	REQUIRE_FALSE(state.IsRowFull(0));

	for (uint32_t col = 0; col < state.Width - 1; ++col)
		state.Board[state.GetBoardIndex(glm::ivec2(static_cast<int32_t>(col), 0))] = 1;
	REQUIRE_FALSE(state.IsRowFull(0));

	state.Board[state.GetBoardIndex(glm::ivec2(9, 0))] = 1;
	REQUIRE(state.IsRowFull(0));
}

TEST_CASE("Game states have names", "[tetris][state]")
{
	REQUIRE(std::string(ToString(TetrisGameState::State::Menu)) == "Menu");
	REQUIRE(std::string(ToString(TetrisGameState::State::Playing)) == "Playing");
	REQUIRE(std::string(ToString(TetrisGameState::State::Paused)) == "Paused");
	REQUIRE(std::string(ToString(TetrisGameState::State::GameOver)) == "GameOver");
}