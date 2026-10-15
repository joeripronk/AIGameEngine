#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Scene/TetrisManager.h"
#include "Forge/Scene/TetrisTypes.h"
#include "Forge/Scene/Components.h"

#include <glm/glm.hpp>

TEST_CASE("TetrisManager initializes correctly", "[tetris][core]")
{
	Forge::TetrisManager manager;
	manager.Initialize();

	REQUIRE(manager.GetState().CurrentState == Forge::TetrisGameState::State::Menu);
	REQUIRE(manager.GetState().Score == 0);
	REQUIRE(manager.GetState().Level == 1);
	REQUIRE(manager.GetState().LinesCleared == 0);
}

TEST_CASE("TetrisManager resets correctly", "[tetris][core]")
{
	Forge::TetrisManager manager;
	manager.Initialize();

	manager.StartGame();
	auto& state = manager.GetState();
	state.Score = 100;
	state.Level = 3;
	state.LinesCleared = 15;

	manager.Reset();

	REQUIRE(manager.GetState().CurrentState == Forge::TetrisGameState::State::Menu);
	REQUIRE(manager.GetState().Score == 0);
	REQUIRE(manager.GetState().Level == 1);
	REQUIRE(manager.GetState().LinesCleared == 0);
}

TEST_CASE("TetrisManager starts game correctly", "[tetris][core]")
{
	Forge::TetrisManager manager;
	manager.Initialize();

	manager.StartGame();

	REQUIRE(manager.GetState().CurrentState == Forge::TetrisGameState::State::Playing);
	REQUIRE(manager.GetState().CurrentPiece.State == Forge::TetrisPieceState::Active);
}

TEST_CASE("TetrisManager toggles pause correctly", "[tetris][core]")
{
	Forge::TetrisManager manager;
	manager.Initialize();
	manager.StartGame();

	REQUIRE(manager.GetState().CurrentState == Forge::TetrisGameState::State::Playing);

	manager.TogglePause();
	REQUIRE(manager.GetState().CurrentState == Forge::TetrisGameState::State::Paused);

	manager.TogglePause();
	REQUIRE(manager.GetState().CurrentState == Forge::TetrisGameState::State::Playing);
}

TEST_CASE("Tetromino definitions are correct", "[tetris][tetromino]")
{
	SECTION("I piece")
	{
		auto& def = Forge::TetrisGameState::GetDefinition(Forge::TetrominoType::I);
		REQUIRE(def.shape[0].x == 0);
		REQUIRE(def.shape[0].y == 0);
		REQUIRE(def.shape[1].x == 1);
		REQUIRE(def.shape[1].y == 0);
		REQUIRE(def.shape[2].x == 2);
		REQUIRE(def.shape[2].y == 0);
		REQUIRE(def.shape[3].x == 3);
		REQUIRE(def.shape[3].y == 0);
	}

	SECTION("O piece")
	{
		auto& def = Forge::TetrisGameState::GetDefinition(Forge::TetrominoType::O);
		REQUIRE(def.shape[0].x == 0);
		REQUIRE(def.shape[0].y == 0);
		REQUIRE(def.shape[1].x == 1);
		REQUIRE(def.shape[1].y == 0);
		REQUIRE(def.shape[2].x == 0);
		REQUIRE(def.shape[2].y == 1);
		REQUIRE(def.shape[3].x == 1);
		REQUIRE(def.shape[3].y == 1);
	}

	SECTION("T piece")
	{
		auto& def = Forge::TetrisGameState::GetDefinition(Forge::TetrominoType::T);
		REQUIRE(def.shape[0].x == 0);
		REQUIRE(def.shape[0].y == 0);
		REQUIRE(def.shape[1].x == 1);
		REQUIRE(def.shape[1].y == 0);
		REQUIRE(def.shape[2].x == 2);
		REQUIRE(def.shape[2].y == 0);
		REQUIRE(def.shape[3].x == 1);
		REQUIRE(def.shape[3].y == 1);
	}
}

TEST_CASE("Piece creation is correct", "[tetris][piece]")
{
	Forge::TetrisManager manager;
	manager.Initialize();

	auto piece = manager.CreatePiece(Forge::TetrominoType::I);

	REQUIRE(piece.Type == Forge::TetrominoType::I);
	REQUIRE(piece.State == Forge::TetrisPieceState::Active);
	REQUIRE(piece.Position.x == 3);
	REQUIRE(piece.Position.y == 0);
	REQUIRE(piece.Rotation == 0);
}

TEST_CASE("Move piece horizontally", "[tetris][movement]")
{
	Forge::TetrisManager manager;
	manager.Initialize();
	manager.StartGame();

	SECTION("Move left")
	{
		manager.MovePiece(Forge::TetrisDirection::Left);
	}

	SECTION("Move right")
	{
		manager.MovePiece(Forge::TetrisDirection::Right);
	}
}

TEST_CASE("Rotate piece works", "[tetris][rotation]")
{
	Forge::TetrisManager manager;
	manager.Initialize();
	manager.StartGame();

	auto& piece = manager.GetState().CurrentPiece;
	piece.Rotation = 0;

	manager.RotatePiece();
	REQUIRE(piece.Rotation == 1);

	manager.RotatePiece();
	REQUIRE(piece.Rotation == 2);

	manager.RotatePiece();
	REQUIRE(piece.Rotation == 3);

	manager.RotatePiece();
	REQUIRE(piece.Rotation == 0);
}

TEST_CASE("Get ghost position works", "[tetris][movement]")
{
	Forge::TetrisManager manager;
	manager.Initialize();
	manager.StartGame();

	auto ghost = manager.GetGhostPosition();

	REQUIRE(ghost.y >= 0);
}

TEST_CASE("Can move to position works", "[tetris][movement]")
{
	Forge::TetrisManager manager;
	manager.Initialize();
	manager.StartGame();

	REQUIRE(manager.CanMoveTo(glm::ivec2(3, 0)) == true);
	REQUIRE(manager.CanMoveTo(glm::ivec2(-1, 0)) == false);
}

TEST_CASE("Lock piece works", "[tetris][locking]")
{
	Forge::TetrisManager manager;
	manager.Initialize();
	manager.StartGame();

	Forge::TetrisGameState& state = manager.GetState();
	state.Width = 10;
	state.Height = 20;
	state.CurrentPiece.Position = glm::ivec2(3, 18);
	state.CurrentPiece.State = Forge::TetrisPieceState::Active;
	state.CurrentPiece.Type = Forge::TetrominoType::I;

	// Lock the piece.
	manager.LockPiece();

	// After LockPiece, check that the NextPiece has been spawned.
	REQUIRE(state.CurrentPiece.Position.x == 3);
	REQUIRE(state.CurrentPiece.Position.y == 0);
}

TEST_CASE("Clear lines works", "[tetris][lines]")
{
	Forge::TetrisManager manager;
	manager.Initialize();

	Forge::TetrisGameState& state = const_cast<Forge::TetrisGameState&>(manager.GetState());
	state.Width = 10;
	state.Height = 20;
	state.InitializeBoard();

	for (uint32_t col = 0; col < 10; ++col)
	{
		state.Board[col] = 1;
	}

	manager.ClearLines();

	for (uint32_t col = 0; col < 10; ++col)
	{
		REQUIRE(state.Board[col] == 0);
	}
}

TEST_CASE("Get line score works", "[tetris][scoring]")
{
	Forge::TetrisManager manager;
	manager.Initialize();

	SECTION("Single line")
	{
		int32_t score = manager.GetLineScore(1, 1, 0);
		REQUIRE(score == 40);
	}

	SECTION("Double line")
	{
		int32_t score = manager.GetLineScore(2, 1, 0);
		REQUIRE(score == 100);
	}

	SECTION("Triple line")
	{
		int32_t score = manager.GetLineScore(3, 1, 0);
		REQUIRE(score == 300);
	}

	SECTION("Tetris (4 lines)")
	{
		int32_t score = manager.GetLineScore(4, 1, 0);
		REQUIRE(score == 1200);
	}

	SECTION("With combo")
	{
		int32_t score = manager.GetLineScore(1, 1, 1);
		REQUIRE(score == 90);
	}
}

TEST_CASE("Get drop interval works", "[tetris][timing]")
{
	Forge::TetrisManager manager;
	manager.Initialize();

	Forge::TetrisGameState& state = const_cast<Forge::TetrisGameState&>(manager.GetState());
	state.Level = 1;
	REQUIRE(manager.GetDropInterval() == Approx(0.8f).epsilon(0.01f));

	state.Level = 5;
	REQUIRE(manager.GetDropInterval() < 0.8f);

	state.Level = 10;
	REQUIRE(manager.GetDropInterval() < 0.7f);
}

TEST_CASE("Tetris game component", "[tetris][component]")
{
	Forge::TetrisGameComponent component;

	REQUIRE(component.IsRunning == false);
	REQUIRE(component.IsPaused == false);
	REQUIRE(component.IsGameOver == false);
	REQUIRE(component.BoardPosition == glm::vec3(0.0f, -10.0f, 0.0f));
	REQUIRE(component.BoardWidth == 10.0f);
	REQUIRE(component.BoardHeight == 20.0f);
	REQUIRE(component.BlockSize == 1.0f);
}

TEST_CASE("Tetris piece component", "[tetris][component]")
{
	Forge::TetrisPieceComponent component;

	REQUIRE(component.Type == 0);
	REQUIRE(component.State == 0);
	REQUIRE(component.GridPosition == glm::ivec2(0, 0));
	REQUIRE(component.VisualOffset == glm::vec3(0.0f));
	REQUIRE(component.Color == glm::vec3(1.0f));
	REQUIRE(component.Scale == 1.0f);
	REQUIRE(component.Alpha == 1.0f);
}

TEST_CASE("Tetris board component", "[tetris][component]")
{
	Forge::TetrisBoardComponent component;

	REQUIRE(component.Width == 10);
	REQUIRE(component.Height == 20);
	REQUIRE(component.Board.empty());
	REQUIRE(component.LinesToClear.empty());
}

TEST_CASE("Tetris display component", "[tetris][component]")
{
	Forge::TetrisDisplayComponent component;

	REQUIRE(component.Score == 0);
	REQUIRE(component.Level == 1);
	REQUIRE(component.LinesCleared == 0);
	REQUIRE(component.Combo == 0);
	REQUIRE(component.NextPiece == 0);
	REQUIRE(component.GameStateText == "Menu");
}

TEST_CASE("TetrisGameState initializes board correctly", "[tetris][state]")
{
	Forge::TetrisGameState state;
	state.Width = 10;
	state.Height = 20;
	state.InitializeBoard();

	REQUIRE(state.Board.size() == 200);
	REQUIRE(state.LinesToClear.size() == 20);

	for (const auto& cell : state.Board)
	{
		REQUIRE(cell == 0);
	}
}

TEST_CASE("IsValidPosition checks bounds correctly", "[tetris][state]")
{
	Forge::TetrisGameState state;
	state.Width = 10;
	state.Height = 20;

	REQUIRE(state.IsValidPosition(glm::ivec2(0, 0)) == true);
	REQUIRE(state.IsValidPosition(glm::ivec2(9, 19)) == true);
	REQUIRE(state.IsValidPosition(glm::ivec2(-1, 0)) == false);
	REQUIRE(state.IsValidPosition(glm::ivec2(10, 0)) == false);
	REQUIRE(state.IsValidPosition(glm::ivec2(0, -1)) == false);
	REQUIRE(state.IsValidPosition(glm::ivec2(0, 20)) == false);
}

TEST_CASE("IsWithinBounds checks bounds correctly", "[tetris][state]")
{
	Forge::TetrisGameState state;
	state.Width = 10;
	state.Height = 20;

	REQUIRE(state.IsWithinBounds(glm::ivec2(0, 0)) == true);
	REQUIRE(state.IsWithinBounds(glm::ivec2(5, 10)) == true);
	REQUIRE(state.IsWithinBounds(glm::ivec2(9, 19)) == true);
}

TEST_CASE("GetBoardIndex works correctly", "[tetris][state]")
{
	Forge::TetrisGameState state;
	state.Width = 10;
	state.Height = 20;

	REQUIRE(state.GetBoardIndex(glm::ivec2(0, 0)) == 0);
	REQUIRE(state.GetBoardIndex(glm::ivec2(9, 0)) == 9);
	REQUIRE(state.GetBoardIndex(glm::ivec2(0, 1)) == 10);
	REQUIRE(state.GetBoardIndex(glm::ivec2(5, 5)) == 55);
}

TEST_CASE("GetGridPosition works correctly", "[tetris][state]")
{
	Forge::TetrisGameState state;
	state.Width = 10;
	state.Height = 20;

	REQUIRE(state.GetGridPosition(0) == glm::ivec2(0, 0));
	REQUIRE(state.GetGridPosition(9) == glm::ivec2(9, 0));
	REQUIRE(state.GetGridPosition(10) == glm::ivec2(0, 1));
	REQUIRE(state.GetGridPosition(55) == glm::ivec2(5, 5));
}

TEST_CASE("CreateDefinition creates correct tetromino", "[tetris][state]")
{
	SECTION("I")
	{
		auto def = Forge::TetrisGameState::CreateDefinition(Forge::TetrominoType::I);
		REQUIRE(def.color.x == 0.0f);
		REQUIRE(def.color.y == 1.0f);
		REQUIRE(def.color.z == 1.0f);
	}

	SECTION("O")
	{
		auto def = Forge::TetrisGameState::CreateDefinition(Forge::TetrominoType::O);
		REQUIRE(def.color.x == 1.0f);
		REQUIRE(def.color.y == 1.0f);
		REQUIRE(def.color.z == 0.0f);
	}
}

