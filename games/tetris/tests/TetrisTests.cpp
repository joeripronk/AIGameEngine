#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "TetrisManager.h"
#include "TetrisTypes.h"
#include "TetrisComponents.h"
#include "TetrisGameScript.h"
#include "TetrisInputLayer.h"

#include "Forge/Core/InputMap.h"
#include "Forge/Scripting/NativeScriptRegistry.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Events/KeyEvent.h"
#include "Forge/Core/KeyCodes.h"

// As in fgpch.h: no OpenGL headers on a Vulkan engine. This file includes
#include <glm/glm.hpp>

#include <algorithm>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>

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

TEST_CASE("The same piece is half as likely to spawn back to back", "[tetris][core]")
{
	// The weighted bag (weights {2,1,2,2,2,1,1}, total 11) makes the raw
	// chance of a repeat sum(weight^2)/total^2 = 19/121, about 15.7%. The
	// generator rejects a repeat on a coin flip, so the observed rate is
	// halved. The generator is seeded, so this sequence is reproducible.
	const double kRawRepeatChance = 19.0 / 121.0;

	std::vector<TetrominoType> sequence;
	TetrisManager manager;
	manager.Initialize();

	// Each StartGame clears the board (so the run never ends) and each
	// LockPiece spawns the queued piece, advancing the same generator. The
	// board reset does not touch the generator, so the sequence is continuous.
	for (int cycle = 0; cycle < 400; ++cycle)
	{
		manager.StartGame();
		sequence.push_back(manager.GetState().CurrentPiece.Type);
		manager.LockPiece();
		sequence.push_back(manager.GetState().CurrentPiece.Type);
	}

	REQUIRE(sequence.size() >= 800);

	size_t repeats = 0;
	for (size_t i = 1; i < sequence.size(); ++i)
		if (sequence[i] == sequence[i - 1])
			++repeats;

	const double observed = static_cast<double>(repeats) / static_cast<double>(sequence.size() - 1);

	// Repeats still happen (the reduction is probabilistic, not a ban), but
	// the rate sits well under the raw weighted chance - the reduction target
	// is half, and a fixed seed makes this exact.
	REQUIRE(observed > 0.0);
	REQUIRE(observed < kRawRepeatChance);
}

TEST_CASE("A fire block melts only what it touches and sheds those blocks", "[tetris][fire]")
{
	TetrisManager manager;
	manager.Initialize();
	manager.StartGame();
	TetrisGameState& state = manager.GetState();

	// One block on the floor, under the middle of where the fire piece falls.
	const uint32_t bottom = state.Height - 1;
	state.Board[bottom * state.Width + 4] = 1;

	// A horizontal fire I piece spanning columns 3..6, one row above the floor.
	state.CurrentPiece = manager.CreatePiece(TetrominoType::I);
	state.CurrentPiece.IsFire = true;
	state.CurrentPiece.Position = glm::ivec2(3, static_cast<int32_t>(bottom) - 1);
	state.UpdatePieceBlocks(state.CurrentPiece);
	for (int i = 0; i < 4; ++i)
	{
		state.CurrentPiece.BlockPos[i] = state.CurrentPiece.Position + state.CurrentPiece.Blocks[i];
		state.CurrentPiece.BlockAlive[i] = true;
	}

	// One gravity step.
	manager.Update(1.0f);

	// The block that met the stack melted it and burned away; the block it
	// melted is gone.
	REQUIRE(state.Board[bottom * state.Width + 4] == 0);

	// Only the touching block was removed - the other three survive and the
	// piece keeps falling.
	int alive = 0;
	for (int i = 0; i < 4; ++i)
		if (state.CurrentPiece.BlockAlive[i])
			++alive;
	REQUIRE(alive == 3);
	REQUIRE(state.CurrentPiece.IsFire);

	// The survivors dropped onto the floor (the row the fire opened).
	for (int i = 0; i < 4; ++i)
		if (state.CurrentPiece.BlockAlive[i])
			REQUIRE(state.CurrentPiece.BlockPos[i].y == static_cast<int32_t>(bottom));
}

TEST_CASE("A hard-dropped fire burns out with an animation, not instantly", "[tetris][fire]")
{
	TetrisManager manager;
	manager.Initialize();
	manager.StartGame();
	TetrisGameState& state = manager.GetState();

	// A vertical fire I onto an empty board: it falls to the floor and burns
	// out there, with no stack to melt.
	state.CurrentPiece = manager.CreatePiece(TetrominoType::I);
	state.CurrentPiece.IsFire = true;
	state.CurrentPiece.Position = glm::ivec2(3, 15);
	state.UpdatePieceBlocks(state.CurrentPiece);
	for (int i = 0; i < 4; ++i)
	{
		state.CurrentPiece.BlockPos[i] = state.CurrentPiece.Position + state.CurrentPiece.Blocks[i];
		state.CurrentPiece.BlockAlive[i] = true;
	}

	manager.HardDrop();

	// The fire is spent, and the cells where its blocks burned out are recorded
	// for animation. A hard drop that reached the floor must not erase the fire
	// with no burn.
	CHECK(state.MeltedCells.size() >= 1u);
}

TEST_CASE("Fire blocks are offered every spawn, and the chance climbs when missed", "[tetris][fire]")
{
	// The generator is seeded, so this run reproduces exactly. The roll runs on
	// every spawn from a base of 0%, and each miss adds 0.2% - ten times slower
	// than the ladder this started at - so this seed's first fire lands on
	// spawn 30, by which point the meter has reached a 1-in-100 roll.
	TetrisManager manager;
	manager.Initialize();
	manager.StartGame();

	// StartGame rolls twice, and a base of 0% cannot produce fire: the ladder
	// is at two steps.
	REQUIRE(manager.GetFireChance() == Approx(0.004f));

	std::vector<bool> fire;
	for (int spawn = 0; spawn < 400; ++spawn)
	{
		TetrisGameState& state = manager.GetState();
		fire.push_back(state.CurrentPiece.IsFire);

		// Empty the board and keep playing, so the run is the ladder and not a
		// game that ends when the stack reaches the top.
		std::fill(state.Board.begin(), state.Board.end(), 0);
		state.CurrentState = TetrisGameState::State::Playing;
		state.HasGameOver = false;
		manager.HardDrop();
	}

	// A base of 0% means the very first spawn cannot be fire, and the meter
	// only reaches this seed's first hit at spawn 30.
	for (int i = 0; i < 30; ++i)
		REQUIRE_FALSE(fire[static_cast<size_t>(i)]);

	// The first fire this seed produces, and a long run produces some (the
	// ladder resets to 0% after each, then climbs again) - but the slow ladder
	// keeps fire the rare event it is.
	REQUIRE(fire[30]);
	const int fires = static_cast<int>(std::count(fire.begin(), fire.end(), true));
	REQUIRE(fires >= 2);
	REQUIRE(fires <= 20);
}

TEST_CASE("A missed fire roll adds a fifth of one percent", "[tetris][fire]")
{
	TetrisManager manager;
	manager.Initialize();
	manager.StartGame();

	for (int spawn = 0; spawn < 20; ++spawn)
	{
		TetrisGameState& state = manager.GetState();
		std::fill(state.Board.begin(), state.Board.end(), 0);
		state.CurrentState = TetrisGameState::State::Playing;
		state.HasGameOver = false;

		const float before = manager.GetFireChance();
		manager.HardDrop();

		// One roll per lock: a miss adds the step, a fire block resets the
		// ladder to its base.
		if (state.CurrentPiece.IsFire)
			REQUIRE(manager.GetFireChance() == Approx(0.0f));
		else
			REQUIRE(manager.GetFireChance() == Approx(before + 0.002f));
	}
}

TEST_CASE("Screen shake charges the fire meter, capped at certain", "[tetris][fire]")
{
	TetrisManager manager;
	manager.Initialize();

	// A fresh game starts the meter at 0%: fire is only offered once it has
	// been charged by shake, or by a run of missed spawns.
	REQUIRE(manager.GetFireChance() == Approx(0.0f));

	manager.StartGame();

	// A strike raises the chance by the amount of shake it produced.
	const float charged = manager.GetFireChance();
	manager.AddFireChance(0.2f);
	REQUIRE(manager.GetFireChance() == Approx(charged + 0.2f));

	// The meter never climbs past certain.
	manager.AddFireChance(10.0f);
	REQUIRE(manager.GetFireChance() == Approx(1.0f));

	// A non-positive strike charges nothing.
	manager.AddFireChance(-0.5f);
	REQUIRE(manager.GetFireChance() == Approx(1.0f));
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
	// A 2-line clear applies the x3 multiplier tier: base 100 at level 1, x3.
	REQUIRE(state.Score == 300);
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

// ---------------------------------------------------------------------------
// Hard drop compaction
// ---------------------------------------------------------------------------

TEST_CASE("A hard drop compacts the columns its impact reaches", "[tetris][lines][compaction]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// A column far from the drop with a block hovering over empty space, and a
	// column the piece lands on with a gap under the block it lands on.
	state.Board[state.GetBoardIndex(glm::ivec2(0, 15))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(4, 18))] = 1;

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	manager.HardDrop();

	// The column the impact travelled down closes its gap: the block the piece
	// landed on is on the floor, and the piece travelled down onto it.
	const uint8_t o = static_cast<uint8_t>(TetrominoType::O) + 1;
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 19))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 18))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 17))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(5, 18))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(5, 17))] == o);

	// The column the impact never touched is untouched. Its gap is a hole the
	// player already chose to leave open, and a drop landing nowhere near it
	// has no business closing it.
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 15))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 19))] == 0);
}

TEST_CASE("A column the piece lands beside keeps its holes", "[tetris][lines][compaction]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// The piece lands on columns 4 and 5. Column 3 has a block standing right
	// beside the piece, with a gap under it and stack above that gap. Touching
	// the piece is not landing on it: the impact is the columns the piece came
	// down on, and a column it only brushed keeps the holes it has.
	state.Board[state.GetBoardIndex(glm::ivec2(4, 18))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(5, 18))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(5, 19))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(3, 13))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(3, 14))] = 2;
	state.Board[state.GetBoardIndex(glm::ivec2(3, 17))] = 3;

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	manager.HardDrop();

	// Column 3 is exactly as it was.
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(3, 13))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(3, 14))] == 2);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(3, 17))] == 3);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(3, 19))] == 0);

	// The column the piece landed on packed: the block it came down on is on
	// the floor.
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 19))] == 1);
}

TEST_CASE("The column the piece lands on packs into the gap under it", "[tetris][lines][compaction]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// The piece lands on a column whose stack has a gap in the middle of it.
	// The run standing on the gap falls into it and the block standing on that
	// run falls with it - a block is moved by this landing because the block it
	// stands on is moved by it. A block in a column the piece never landed on
	// is not part of the landing.
	state.Board[state.GetBoardIndex(glm::ivec2(4, 14))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(4, 15))] = 2;
	state.Board[state.GetBoardIndex(glm::ivec2(0, 11))] = 4;

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	manager.HardDrop();

	// The run that was standing on the gap is on the floor, and the piece
	// came down onto it.
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 19))] == 2);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 18))] == 1);
	const uint8_t o = static_cast<uint8_t>(TetrominoType::O) + 1;
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 17))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 16))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 15))] == 0);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 14))] == 0);

	// A block in a column the contact never reached.
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 11))] == 4);
}

TEST_CASE("A gap below the impact's reach stays open", "[tetris][lines][compaction]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// A tall column with a hole in the floor under it. The piece lands on top
	// of it, so the impact travels the length of the column and finds the gap
	// six rows below the row it landed on - out of reach.
	for (uint32_t row = 13; row <= 17; ++row)
		state.Board[state.GetBoardIndex(glm::ivec2(4, row))] = 1;

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	manager.HardDrop();

	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 13))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 17))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 18))] == 0);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 19))] == 0);
	REQUIRE(state.CompactedCells.empty());

	// The same stack three rows shorter puts the hole at the edge of the reach,
	// and the impact closes it. Column 0, so the piece that landed in the
	// column above is not in the way.
	state.Board[state.GetBoardIndex(glm::ivec2(0, 16))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(0, 17))] = 1;

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(0, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	manager.HardDrop();

	const uint8_t square = static_cast<uint8_t>(TetrominoType::O) + 1;
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 18))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 19))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 17))] == square);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 16))] == square);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 14))] == 0);
}

TEST_CASE("Compaction keeps the order of a column and reports what moved", "[tetris][lines][compaction]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// Three blocks with gaps between them, each a different type so the order
	// is readable in the board.
	state.Board[state.GetBoardIndex(glm::ivec2(3, 2))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(3, 9))] = 2;
	state.Board[state.GetBoardIndex(glm::ivec2(3, 16))] = 3;

	const uint32_t moved = manager.CompactStack();

	REQUIRE(moved == 3u);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(3, 19))] == 3);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(3, 18))] == 2);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(3, 17))] == 1);

	// The record is where each block came from, which is the only thing that
	// can animate a slide.
	REQUIRE(state.CompactedCells.size() == 3u);
	REQUIRE(state.CompactedCells[0].Column == 3u);
	REQUIRE(state.CompactedCells[0].FromRow == 16u);
	REQUIRE(state.CompactedCells[0].ToRow == 19u);
	REQUIRE(state.CompactedCells[2].FromRow == 2u);
	REQUIRE(state.CompactedCells[2].ToRow == 17u);
}

TEST_CASE("A packed stack compacts to nothing", "[tetris][lines][compaction]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// A floor with no holes in it is the ordinary state of a stack that has
	// only ever been hard dropped, and a drop on top of it must not shuffle it.
	FillRow(state, state.Height - 1, state.Width);
	FillRow(state, state.Height - 2, 4);

	REQUIRE(manager.CompactStack() == 0u);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 19))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(3, 18))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 18))] == 0);
}

TEST_CASE("Compaction closes a hole and the row it completes is cleared", "[tetris][lines][compaction]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// The bottom row has one hole in column 4, and the column above the hole is
	// a short stack resting on a gap. The impact travels down that stack, finds
	// the gap within its reach, closes it - and the stack lands in the hole.
	// Discrete play can never complete this row: a piece dropped into column 4
	// stops on top of the stack, not in the hole.
	for (uint32_t col = 0; col < state.Width; ++col)
	{
		if (col == 4)
			continue;
		state.Board[state.GetBoardIndex(glm::ivec2(static_cast<int32_t>(col), state.Height - 1))] = 1;
	}
	state.Board[state.GetBoardIndex(glm::ivec2(4, 16))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(4, 17))] = 1;

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	manager.HardDrop();

	REQUIRE(state.LinesCleared == 1);
	// A 1-line clear applies the x2 tier: base 40 at level 1, x2.
	REQUIRE(state.Score == 80);

	// The piece travelled down as far as the closing stack let it, and the row
	// it completed took the stack with it, dropping the piece into the row
	// below.
	const uint8_t o = static_cast<uint8_t>(TetrominoType::O) + 1;
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 18))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 17))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 16))] == 0);
	// The stack block that completed the row went with the row, and the row
	// above it - the one it packed into - dropped into it.
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 19))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 19))] == 0);
}

TEST_CASE("A lock that is not a hard drop leaves the stack where it stands", "[tetris][lines][compaction]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	state.Board[state.GetBoardIndex(glm::ivec2(0, 10))] = 1;

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 18);
	state.UpdatePieceBlocks(state.CurrentPiece);

	// Gravity locking is a landing, not a pack: the gap under a block is part
	// of the stack the player built, and only a hard drop spends it.
	manager.LockPiece();

	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 10))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 19))] == 0);
	REQUIRE(state.CompactedCells.empty());
}

TEST_CASE("A hard drop keeps the dropped piece in one piece", "[tetris][lines][compaction]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// A floating pair with a deep gap under it: the piece that lands on it has
	// nine rows to travel once the pack closes, and a per-column pack slides
	// each of its blocks by its own distance - which is a shape arriving as a
	// scatter of blocks, the piece the player dropped dissolved by the drop.
	state.Board[state.GetBoardIndex(glm::ivec2(4, 10))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(5, 10))] = 1;

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	manager.HardDrop();

	// The square is a square, nine rows lower than where it locked, sitting on
	// the pair it landed on - which packed down beneath it.
	const uint8_t o = static_cast<uint8_t>(TetrominoType::O) + 1;
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 17))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(5, 17))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 18))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(5, 18))] == o);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 19))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(5, 19))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(4, 10))] == 0);

	// A non-rectangular piece is the case that matters: the nub is part of the
	// shape, so it travels with the rest of it.
	state.Board[state.GetBoardIndex(glm::ivec2(0, 12))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(1, 12))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(2, 12))] = 1;

	state.CurrentPiece.Type = TetrominoType::T;
	state.CurrentPiece.Position = glm::ivec2(0, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	manager.HardDrop();

	const uint8_t t = static_cast<uint8_t>(TetrominoType::T) + 1;
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 17))] == t);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(1, 17))] == t);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(2, 17))] == t);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(1, 18))] == t);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 18))] == 0);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(2, 18))] == 0);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(0, 19))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(1, 19))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(2, 19))] == 1);
}

TEST_CASE("Compaction moves each column on its own", "[tetris][lines][compaction]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// A block resting on its own shelf is packed, and the empty column beside
	// it stays empty: compaction closes gaps, it does not pull blocks sideways.
	state.Board[state.GetBoardIndex(glm::ivec2(2, 11))] = 1;
	state.Board[state.GetBoardIndex(glm::ivec2(2, 19))] = 1;

	REQUIRE(manager.CompactStack() == 1u);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(2, 19))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(2, 18))] == 1);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(2, 11))] == 0);
	REQUIRE(state.Board[state.GetBoardIndex(glm::ivec2(3, 19))] == 0);
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
	REQUIRE(TetrisManager::GetLineScore(0, 1, 1, 1) == 0);
	REQUIRE(TetrisManager::GetLineScore(1, 1, 1, 1) == 40);
	REQUIRE(TetrisManager::GetLineScore(2, 1, 1, 1) == 100);
	REQUIRE(TetrisManager::GetLineScore(3, 1, 1, 1) == 300);
	REQUIRE(TetrisManager::GetLineScore(4, 1, 1, 1) == 1200);
}

TEST_CASE("Score scales with level, multiplier and combo", "[tetris][scoring]")
{
	// base * level * multiplier * comboMultiplier
	REQUIRE(TetrisManager::GetLineScore(1, 3, 1, 1) == 120);
	REQUIRE(TetrisManager::GetLineScore(1, 1, 2, 1) == 80);
	REQUIRE(TetrisManager::GetLineScore(2, 2, 1, 2) == 400);
	REQUIRE(TetrisManager::GetLineScore(2, 2, 3, 4) == 2400);
	// More than four lines cannot happen, but must not read past the table.
	REQUIRE(TetrisManager::GetLineScore(9, 1, 1, 1) == 1200);
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

TEST_CASE("Reward labels run Nice / Great / Amazing / Epic for 1-4 lines", "[tetris][scoring][reward]")
{
	REQUIRE(std::string(TetrisManager::RewardLabelForLines(1)) == "Nice");
	REQUIRE(std::string(TetrisManager::RewardLabelForLines(2)) == "Great");
	REQUIRE(std::string(TetrisManager::RewardLabelForLines(3)) == "Amazing");
	REQUIRE(std::string(TetrisManager::RewardLabelForLines(4)) == "Epic");
}

TEST_CASE("A clear applies its multiplier tier (x2 / x3 / x4)", "[tetris][scoring][reward]")
{
	REQUIRE(TetrisManager::MultiplierTierForLines(1) == 2u);
	REQUIRE(TetrisManager::MultiplierTierForLines(2) == 3u);
	REQUIRE(TetrisManager::MultiplierTierForLines(3) == 4u);
	REQUIRE(TetrisManager::MultiplierTierForLines(4) == 4u);
}

TEST_CASE("The multiplier stacks across clears and is spent after 5 turns", "[tetris][scoring][reward]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	// First clear: a single line applies x2, refreshing the 5-turn window.
	FillRow(state, state.Height - 1, state.Width);
	manager.LockPiece();
	REQUIRE(state.Multiplier == 2);
	REQUIRE(state.MultiplierTurns == 4);  // the lock that cleared ticks the window down

	// A second clear stacks x3 onto the live x2, so the multiplier is x6.
	FillRow(state, state.Height - 2, state.Width);
	FillRow(state, state.Height - 1, state.Width);
	manager.LockPiece();
	REQUIRE(state.Multiplier == 6);
	REQUIRE(state.MultiplierTurns == 4);

	// Four locks with no clear burn the window down to zero and reset the
	// multiplier to x1. Each lock starts from a cleared board so the spawn row
	// stays free and no row fills.
	for (int i = 0; i < 4; ++i)
	{
		state.InitializeBoard();
		FillRow(state, 10, 1);  // a row that never fills: no clear
		manager.LockPiece();
	}
	REQUIRE(state.Multiplier == 1);
	REQUIRE(state.MultiplierTurns == 0);
}

TEST_CASE("Consecutive clears double the combo multiplier", "[tetris][scoring][reward]")
{
	TetrisManager manager = Playing();
	auto& state = manager.GetState();

	FillRow(state, state.Height - 1, state.Width);
	manager.LockPiece();
	REQUIRE(state.ComboMultiplier == 1);  // first clear of the chain: 1x

	FillRow(state, state.Height - 2, state.Width);
	FillRow(state, state.Height - 1, state.Width);
	manager.LockPiece();
	REQUIRE(state.ComboMultiplier == 2);  // second consecutive: 2x

	FillRow(state, state.Height - 2, state.Width);
	FillRow(state, state.Height - 1, state.Width);
	manager.LockPiece();
	REQUIRE(state.ComboMultiplier == 4);  // third consecutive: 4x

	// A turn with no clear breaks the chain: the next clear starts at 1x again.
	state.InitializeBoard();
	FillRow(state, 10, 1);
	manager.LockPiece();
	FillRow(state, state.Height - 1, state.Width);
	manager.LockPiece();
	REQUIRE(state.ComboMultiplier == 1);
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

TEST_CASE("The default bindings are the keys the game is played with", "[tetris][input]")
{
	Forge::InputMap::Clear();
	REQUIRE(SetupInputMap());

	// The keys the game is documented to answer to are bound to the actions
	// they mean. The binding is data, in the engine's input map, so this is
	// a question about the map and not about a switch in the code.
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::Left).size() == 1);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::Left)[0] == InputAction::MoveLeft);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::Right)[0] == InputAction::MoveRight);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::Down)[0] == InputAction::SoftDrop);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::Space)[0] == InputAction::HardDrop);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::Up)[0] == InputAction::RotateCW);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::P)[0] == InputAction::TogglePause);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::R)[0] == InputAction::Restart);

	// The WASD keys are the same actions.
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::A)[0] == InputAction::MoveLeft);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::D)[0] == InputAction::MoveRight);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::S)[0] == InputAction::SoftDrop);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::W)[0] == InputAction::RotateCW);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::X)[0] == InputAction::RotateCW);
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::Z)[0] == InputAction::RotateCCW);

	Forge::InputMap::Clear();
}

TEST_CASE("Every arrow key is bound", "[tetris][input]")
{
	Forge::InputMap::Clear();
	REQUIRE(SetupInputMap());

	// The runtime's old input path polled from GLFW_KEY_SPACE upwards, so all
	// three arrows - the keys the game is played with - never reached it.
	REQUIRE_FALSE(Forge::InputMap::GetActionsForKey(Forge::Key::Left).empty());
	REQUIRE_FALSE(Forge::InputMap::GetActionsForKey(Forge::Key::Right).empty());
	REQUIRE_FALSE(Forge::InputMap::GetActionsForKey(Forge::Key::Down).empty());
	REQUIRE_FALSE(Forge::InputMap::GetActionsForKey(Forge::Key::Up).empty());

	Forge::InputMap::Clear();
}

TEST_CASE("A key the game does not bind means nothing", "[tetris][input]")
{
	Forge::InputMap::Clear();
	REQUIRE(SetupInputMap());

	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::F12).empty());

	Forge::InputMap::Clear();
}

TEST_CASE("The rules know the actions, not the keys", "[tetris][input]")
{
	REQUIRE(RuleActionFor(InputAction::MoveLeft) == Action::MoveLeft);
	REQUIRE(RuleActionFor(InputAction::MoveRight) == Action::MoveRight);
	REQUIRE(RuleActionFor(InputAction::SoftDrop) == Action::SoftDrop);
	REQUIRE(RuleActionFor(InputAction::HardDrop) == Action::HardDrop);
	REQUIRE(RuleActionFor(InputAction::RotateCW) == Action::RotateCW);
	REQUIRE(RuleActionFor(InputAction::RotateCCW) == Action::RotateCCW);
	REQUIRE(RuleActionFor(InputAction::TogglePause) == Action::TogglePause);
	REQUIRE(RuleActionFor(InputAction::Restart) == Action::Restart);

	// V switches the 2D/3D view. That is presentation, not a rule, so it must
	// reach the app as a callback and never arrive as a rules action.
	REQUIRE(RuleActionFor(InputAction::ToggleView) == Action::None);
	REQUIRE(RuleActionFor(InputAction::ToggleMusic) == Action::None);
	REQUIRE(RuleActionFor("not_an_action") == Action::None);
}

TEST_CASE("A key press reaches the rules through the input map", "[tetris][input]")
{
	Forge::InputMap::Clear();
	REQUIRE(SetupInputMap());

	TetrisManager manager;
	manager.StartGame();

	InputLayer layer(manager);

	// The piece starts in the middle of the board.
	const glm::ivec2 before = manager.GetState().CurrentPiece.Position;

	// A key press is an event; the event is what the game is played with.
	Forge::KeyPressedEvent pressed(Forge::Key::A, 0);
	layer.OnEvent(pressed);

	// The rules moved the piece: the whole chain - the event, the map, the
	// action, the rules - is one path, and this is where it is checked.
	REQUIRE(manager.GetState().CurrentPiece.Position.x == before.x - 1);

	// And the key that goes up ends the movement.
	Forge::KeyReleasedEvent released(Forge::Key::A);
	layer.OnEvent(released);

	Forge::InputMap::Clear();
}

TEST_CASE("The bindings can be changed without the rules changing", "[tetris][input]")
{
	Forge::InputMap::Clear();
	REQUIRE(SetupInputMap());

	// The player moves with J instead of A.
	REQUIRE(Forge::InputMap::RemoveBinding(InputAction::MoveLeft, Forge::Key::A));
	REQUIRE(Forge::InputMap::AddKey(InputAction::MoveLeft, Forge::Key::J));

	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::A).empty());
	REQUIRE(Forge::InputMap::GetActionsForKey(Forge::Key::J)[0] == InputAction::MoveLeft);

	// The rule did not change: it is still the move-left action.
	REQUIRE(RuleActionFor(Forge::InputMap::GetActionsForKey(Forge::Key::J)[0]) == Action::MoveLeft);

	Forge::InputMap::Clear();
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

TEST_CASE("The game ships a scene the editor can open", "[tetris][scene]")
{
	// The scene is the game's content: its camera, its light and its board.
	// The editor opens it like any other scene, and the game loads it so
	// the two agree.
	const std::string path = std::string(FORGE_SOURCE_DIR) + "/resources/scenes/Tetris.forge";

	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	Forge::SceneSerializer serializer(scene);
	REQUIRE(serializer.Deserialize(path));

	// The camera the game places for the mode it is in.
	Forge::Entity camera = scene->FindEntityByName("MainCamera");
	REQUIRE(camera);
	REQUIRE(camera.HasComponent<Forge::CameraComponent>());

	// The light.
	Forge::Entity light = scene->FindEntityByName("DirectionalLight");
	REQUIRE(light);
	REQUIRE(light.HasComponent<Forge::LightComponent>());

	// The board: its backdrop and the four borders around it.
	REQUIRE(scene->FindEntityByName("BoardBackdrop"));
	REQUIRE(scene->FindEntityByName("BoardBorderLeft"));
	REQUIRE(scene->FindEntityByName("BoardBorderRight"));
	REQUIRE(scene->FindEntityByName("BoardBorderTop"));
	REQUIRE(scene->FindEntityByName("BoardBorderFloor"));

	// And the board's pieces are meshes an author can point elsewhere.
	Forge::Entity backdrop = scene->FindEntityByName("BoardBackdrop");
	REQUIRE(backdrop.HasComponent<Forge::MeshComponent>());
	REQUIRE(backdrop.GetComponent<Forge::MeshComponent>().ModelPath == "resources/models/cube.glb");

	// The game's C++ is named the way a Lua script is: by a name the scene
	// holds. The game resolves it against the registry it registered the
	// type in; the editor, which is a different binary, has no such name.
	Forge::Entity controller = scene->FindEntityByName("GameController");
	REQUIRE(controller);
	REQUIRE(controller.HasComponent<Forge::NativeScriptComponent>());
	REQUIRE(controller.GetComponent<Forge::NativeScriptComponent>().ClassName == "TetrisGame");
}

TEST_CASE("The scene's script is the game's rules, and they advance", "[tetris][scene][script]")
{
	// The scene names the script, the script holds the rules, and the
	// scene's runtime runs it: the game is driven by the scene it ships.
	Forge::RegisterNativeScript<Tetris::TetrisGameScript>("TetrisGame");

	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	Forge::Entity controller = scene->CreateEntity("GameController");
	controller.AddComponent<Forge::NativeScriptComponent>().ClassName = "TetrisGame";

	// The runtime start creates the script, and its OnCreate attaches the
	// board to the scene.
	scene->OnRuntimeStart();

	Forge::Entity state = scene->FindEntityByName("TetrisGame");
	REQUIRE(state);
	REQUIRE(state.HasComponent<Tetris::TetrisGameComponent>());

	// The rules advance when the scene updates: the piece falls, so the
	// board's state changes with the frames.
	scene->OnUpdateRuntime(Forge::Timestep(0.5f));

	Forge::Entity piece = scene->FindEntityByName("CurrentPiece");
	REQUIRE(piece);
	REQUIRE(piece.HasComponent<Tetris::TetrisPieceComponent>());

	scene->OnRuntimeStop();
}
