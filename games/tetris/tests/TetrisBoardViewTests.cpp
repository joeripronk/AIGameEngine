#include <catch2/catch_test_macros.hpp>

#include "TetrisBoardView.h"

#include "Forge/Core/Log.h"
#include "Forge/Scene/Scene.h"

#include <string>

using namespace Tetris;

namespace {

// Counts the entities the view has created for board cells.
size_t CountBlockEntities(Forge::Scene& scene)
{
	size_t count = 0;
	for (Forge::Entity entity : scene.GetEntitiesInOrder())
	{
		if (entity.GetName().rfind("TetBlock_", 0) == 0)
			++count;
	}
	return count;
}

bool HasEntityNamed(Forge::Scene& scene, const std::string& name)
{
	return static_cast<bool>(scene.FindEntityByName(name));
}

// A view already running, with a known piece on the board.
struct RunningView
{
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
	BoardView view;

	RunningView()
	{
		Forge::Log::Init();
		// The block model is not needed headless: no mesh is ever loaded unless
		// something renders, and the tests never do.
		REQUIRE(view.Initialize(scene));
		view.Update(0.0f);  // first update starts the game
	}
};

}  // namespace

TEST_CASE("A view with no scene refuses to start", "[tetris][view]")
{
	Forge::Log::Init();

	BoardView view;
	REQUIRE_FALSE(view.Initialize(nullptr));
	// And says so by doing nothing, rather than dereferencing a null scene.
	view.Update(1.0f);
	REQUIRE_FALSE(view.IsInitialized());
}

TEST_CASE("The first update starts the game and draws the piece", "[tetris][view]")
{
	RunningView fixture;

	REQUIRE(fixture.view.GetManager().GetState().CurrentState == TetrisGameState::State::Playing);
	// Four blocks, one per cell of the falling piece.
	REQUIRE(CountBlockEntities(*fixture.scene) == 4);
}

TEST_CASE("The state entities are published into the scene", "[tetris][view]")
{
	RunningView fixture;
	fixture.view.GetManager().GetState().Score = 1234;
	fixture.view.Update(0.0f);

	Forge::Entity display = fixture.scene->FindEntityByName("GameDisplay");
	REQUIRE(display);
	REQUIRE(display.GetComponent<TetrisDisplayComponent>().Score == 1234);
	REQUIRE(display.GetComponent<TetrisDisplayComponent>().GameStateText == "Playing");

	Forge::Entity board = fixture.scene->FindEntityByName("GameBoard");
	REQUIRE(board);
	REQUIRE(board.GetComponent<TetrisBoardComponent>().Board.size() == 200);

	Forge::Entity piece = fixture.scene->FindEntityByName("CurrentPiece");
	REQUIRE(piece);
	REQUIRE(piece.GetComponent<TetrisPieceComponent>().State == TetrisPieceState::Active);
}

TEST_CASE("Clearing a line removes the blocks that filled it", "[tetris][view]")
{
	RunningView fixture;
	auto& state = fixture.view.GetManager().GetState();

	// Pin the falling piece so the counts below are about the board alone, then
	// fill the bottom row: ten more blocks alongside its four.
	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);
	for (uint32_t col = 0; col < state.Width; ++col)
		state.Board[state.GetBoardIndex(glm::ivec2(static_cast<int32_t>(col), 19))] = 1;

	fixture.view.Update(0.0f);
	REQUIRE(CountBlockEntities(*fixture.scene) == 14);
	REQUIRE(HasEntityNamed(*fixture.scene, "TetBlock_0_19"));

	fixture.view.GetManager().ClearLines();
	fixture.view.Update(0.0f);

	// The row is gone, so its ten entities have to be gone with it. Leaving them
	// behind is how a board fills up with the ghost of every cell ever visited.
	REQUIRE_FALSE(HasEntityNamed(*fixture.scene, "TetBlock_0_19"));
	REQUIRE(CountBlockEntities(*fixture.scene) == 4);
}

TEST_CASE("Restarting clears the blocks of the finished game", "[tetris][view]")
{
	RunningView fixture;
	auto& manager = fixture.view.GetManager();

	for (int i = 0; i < 5; ++i)
	{
		manager.HardDrop();
		fixture.view.Update(0.0f);
	}
	REQUIRE(CountBlockEntities(*fixture.scene) > 4);

	fixture.view.Restart();

	// Only the freshly spawned piece remains.
	REQUIRE(CountBlockEntities(*fixture.scene) == 4);
	REQUIRE(manager.GetState().Score == 0);
}

TEST_CASE("Shutdown destroys everything the view created", "[tetris][view]")
{
	Forge::Log::Init();
	Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();

	BoardView view;
	REQUIRE(view.Initialize(scene));
	view.Update(0.0f);
	REQUIRE(CountBlockEntities(*scene) == 4);

	view.Shutdown();

	REQUIRE_FALSE(view.IsInitialized());
	REQUIRE(CountBlockEntities(*scene) == 0);
	REQUIRE_FALSE(HasEntityNamed(*scene, "GameBoard"));
	REQUIRE_FALSE(HasEntityNamed(*scene, "GameDisplay"));
	// The scene itself survives, so the app can still tear it down cleanly.
	REQUIRE(scene->GetEntitiesInOrder().size() == 0);
}

TEST_CASE("Blocks are placed at the centre of their cell", "[tetris][view]")
{
	// The playfield spans 10 by 20 units around the z-offset well, so cell
	// (0,0) is the top row, one block in from the left.
	REQUIRE(BoardView::CellToWorld(0, 0) == glm::vec3(-4.5f, 9.5f, -0.44f));
	REQUIRE(BoardView::CellToWorld(9, 19) == glm::vec3(4.5f, -9.5f, -0.44f));
	REQUIRE(BoardView::BoardCentre() == glm::vec3(0.0f, 0.0f, -0.44f));
	REQUIRE(BoardView::BoardSize() == glm::vec3(10.0f, 20.0f, 1.0f));
}