#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TetrisBoardView.h"

#include "Forge/Core/Log.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <utility>

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

// Where an entity is drawn, so a test can say what the player would see.
glm::vec3 EntityPosition(Forge::Scene& scene, const std::string& name)
{
	Forge::Entity entity = scene.FindEntityByName(name);
	REQUIRE(static_cast<bool>(entity));
	return entity.GetComponent<Forge::TransformComponent>().Translation;
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

	// The row is animating away: its blocks shrink over a short window rather
	// than vanishing in one frame.
	for (int i = 0; i < 120; ++i)
		fixture.view.Update(1.0f / 60.0f);

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
	// The playfield is 10x20 cells of 1.2 world units, spanning 12 by 24 units
	// around the z-offset well, so cell (0,0) is the top row, one block in from
	// the left. Components are compared with Approx: 1.2 is not exact in float,
	// so an exact == on the mapped coordinates is a rounding accident.
	const glm::vec3 corner = BoardView::CellToWorld(0, 0);
	REQUIRE(corner.x == Catch::Approx(-4.4f));
	REQUIRE(corner.y == Catch::Approx(13.4f));
	REQUIRE(corner.z == Catch::Approx(-0.44f));
	const glm::vec3 far = BoardView::CellToWorld(9, 19);
	REQUIRE(far.x == Catch::Approx(6.4f));
	REQUIRE(far.y == Catch::Approx(-9.4f));
	REQUIRE(far.z == Catch::Approx(-0.44f));
	const glm::vec3 centre = BoardView::BoardCentre();
	REQUIRE(centre.x == Catch::Approx(1.0f));
	REQUIRE(centre.y == Catch::Approx(2.0f));
	REQUIRE(centre.z == Catch::Approx(-0.44f));
	const glm::vec3 size = BoardView::BoardSize();
	REQUIRE(size.x == Catch::Approx(12.0f));
	REQUIRE(size.y == Catch::Approx(24.0f));
	REQUIRE(size.z == Catch::Approx(1.0f));
}

TEST_CASE("The preview lays the queued piece out the way it spawns", "[tetris][view]")
{
	// The tray is 4x4 cells and the preview blocks scale with the block size, so
	// a cell centre may sit no further out than 1.5 cells. Measured with the same
	// arithmetic the preview uses, the bound is exactly reachable.
	const float kMaxExtent = 1.5f * BoardView::BlockSize();

	for (uint8_t t = 0; t < static_cast<uint8_t>(TetrominoType::Count); ++t)
	{
		const TetrominoType type = static_cast<TetrominoType>(t);
		INFO("piece index " << static_cast<int>(t));

		glm::vec3 offsets[4];
		BoardView::PreviewLocalOffsets(type, offsets);

		glm::ivec2 spawn[4];
		GetRotatedBlocks(type, 0, spawn);

		float minX = offsets[0].x, maxX = offsets[0].x;
		float minY = offsets[0].y, maxY = offsets[0].y;
		for (int i = 1; i < 4; ++i)
		{
			minX = std::min(minX, offsets[i].x);
			maxX = std::max(maxX, offsets[i].x);
			minY = std::min(minY, offsets[i].y);
			maxY = std::max(maxY, offsets[i].y);
			REQUIRE(offsets[i].z == 0.46f);
		}

		// Centred: a 4-wide I spans +-1.5, a 2-wide O spans +-0.5. Anchoring
		// every shape at one corner put the small pieces off to one side and
		// let the I hang past the tray.
		REQUIRE(minX + maxX == Catch::Approx(0.0f));
		REQUIRE(minY + maxY == Catch::Approx(0.0f));
		REQUIRE(std::abs(minX) <= kMaxExtent);
		REQUIRE(std::abs(maxX) <= kMaxExtent);
		REQUIRE(std::abs(minY) <= kMaxExtent);
		REQUIRE(std::abs(maxY) <= kMaxExtent);

		// The same four cells, in the same orientation: the preview is the
		// piece about to spawn, so a shape mirrored vertically is a preview of
		// a piece that never appears. Rows are measured the way the playfield
		// measures them - row 0 at the top, which is +Y.
		std::set<std::pair<int, int>> expected;
		for (const glm::ivec2& block : spawn)
			expected.insert({ block.x, block.y });

		std::set<std::pair<int, int>> shown;
		const float cell = BoardView::BlockSize();
		for (const glm::vec3& offset : offsets)
			shown.insert({ static_cast<int>(std::lround((offset.x - minX) / cell)),
				static_cast<int>(std::lround((maxY - offset.y) / cell)) });

		REQUIRE(shown == expected);
		REQUIRE(shown.size() == 4);
	}
}
TEST_CASE("The falling piece eases into its next cell", "[tetris][view]")
{
	RunningView fixture;

	const glm::vec3 before = EntityPosition(*fixture.scene, "TetBlock_Piece0");

	fixture.view.HandleAction(Action::MoveLeft, true);
	fixture.view.HandleAction(Action::MoveLeft, false);
	fixture.view.Update(1.0f / 60.0f);

	// One frame after the press the block is on its way, not there: a block
	// that reaches its next cell in one frame is a teleport.
	const glm::vec3 mid = EntityPosition(*fixture.scene, "TetBlock_Piece0");
	REQUIRE(mid.x < before.x);
	REQUIRE(mid.x > before.x - BoardView::BlockSize());
	REQUIRE(mid.y == Catch::Approx(before.y));

	for (int i = 0; i < 30; ++i)
		fixture.view.Update(1.0f / 60.0f);

	// The ease ends where the board says it does, exactly.
	const glm::vec3 settled = EntityPosition(*fixture.scene, "TetBlock_Piece0");
	REQUIRE(settled.x == Catch::Approx(before.x - BoardView::BlockSize()));
	REQUIRE(settled.y == Catch::Approx(before.y));
}

TEST_CASE("A hard drop strikes the locked blocks down into the board", "[tetris][view]")
{
	RunningView fixture;

	float spawnTop = 0.0f;
	for (int i = 0; i < 4; ++i)
		spawnTop = std::max(spawnTop, EntityPosition(*fixture.scene, "TetBlock_Piece" + std::to_string(i)).y);

	fixture.view.HandleAction(Action::HardDrop, true);
	fixture.view.Update(1.0f / 60.0f);

	const TetrisGameState& state = fixture.view.GetManager().GetState();

	std::vector<std::pair<std::string, glm::vec3>> cells;
	for (uint32_t row = 0; row < state.Height; ++row)
	{
		for (uint32_t col = 0; col < state.Width; ++col)
		{
			const uint32_t index = row * state.Width + col;
			if (index >= state.Board.size() || state.Board[index] == 0)
				continue;

			const std::string name = "TetBlock_" + std::to_string(col) + "_" + std::to_string(row);
			const glm::vec3 target = BoardView::CellToWorld(col, row);
			const glm::vec3 drawn = EntityPosition(*fixture.scene, name);

			// The strike is vertical, and on the frame Space was pressed the
			// blocks are between where the piece was and where it landed.
			REQUIRE(drawn.x == Catch::Approx(target.x));
			REQUIRE(drawn.z == Catch::Approx(target.z));
			REQUIRE(drawn.y > target.y);
			REQUIRE(drawn.y < spawnTop);

			cells.emplace_back(name, target);
		}
	}
	REQUIRE(cells.size() == 4);

	for (int i = 0; i < 60; ++i)
		fixture.view.Update(1.0f / 60.0f);

	for (const auto& [name, target] : cells)
	{
		const glm::vec3 drawn = EntityPosition(*fixture.scene, name);
		REQUIRE(drawn == target);
	}
}

TEST_CASE("Impact speed measures how fast the blocks fell", "[tetris][view]")
{
	RunningView fixture;

	float gravitySpeed = -1.0f;
	// A level-1 piece falls one row every 0.8s, and the well is 20 rows deep:
	// a gravity lock needs the better part of a minute of game time.
	for (int i = 0; i < 3000 && gravitySpeed < 0.0f; ++i)
	{
		fixture.view.Update(1.0f / 60.0f);
		if (fixture.view.GetLastImpact().Occurred)
			gravitySpeed = fixture.view.GetLastImpact().Speed;
	}
	REQUIRE(gravitySpeed > 0.0f);

	// A move is not a landing, and must not shake anything.
	fixture.view.HandleAction(Action::MoveLeft, true);
	fixture.view.HandleAction(Action::MoveLeft, false);
	fixture.view.Update(1.0f / 60.0f);
	REQUIRE_FALSE(fixture.view.GetLastImpact().Occurred);

	// A drop from the top of the well is the same lock with far more speed
	// behind it, which is what makes the shake scale with it.
	fixture.view.HandleAction(Action::HardDrop, true);
	fixture.view.Update(1.0f / 60.0f);
	REQUIRE(fixture.view.GetLastImpact().Occurred);
	REQUIRE(fixture.view.GetLastImpact().Speed > gravitySpeed * 10.0f);
	REQUIRE(fixture.view.GetLastImpact().Rows > 10.0f);
}

TEST_CASE("A falling piece glides at the drop rate instead of hopping", "[tetris][view]")
{
	RunningView fixture;

	std::vector<float> ys;
	for (int i = 0; i < 240; ++i)
	{
		fixture.view.Update(1.0f / 60.0f);
		ys.push_back(EntityPosition(*fixture.scene, "TetBlock_Piece0").y);
	}

	// The first downward step: the last frame before it moves, and the last
	// frame in which it does.
	size_t start = 0;
	while (start + 1 < ys.size() && ys[start + 1] >= ys[start] - 1e-4f)
		++start;
	REQUIRE(start + 1 < ys.size());

	size_t end = start + 1;
	while (end + 1 < ys.size() && ys[end + 1] < ys[end] - 1e-4f)
		++end;

	// One row, covered completely - not a hop that arrives and then sits there
	// for the rest of the drop interval.
	REQUIRE(ys[start] - ys[end] == Catch::Approx(BoardView::BlockSize()).margin(0.001f));
	REQUIRE(static_cast<int>(end - start) >= 20);

	// Constant rate. Easing each row makes the block accelerate and brake once
	// per row, which is a stutter at the frequency the fall is meant to be
	// smooth at, so no frame may cover more than twice what any other covers.
	float smallest = 1.0e9f;
	float largest = 0.0f;
	for (size_t i = start; i < end; ++i)
	{
		const float delta = ys[i] - ys[i + 1];
		REQUIRE(delta > 0.0f);
		smallest = std::min(smallest, delta);
		largest = std::max(largest, delta);
	}
	REQUIRE(largest < smallest * 2.0f);
}

TEST_CASE("A melted block burns out and leaves nothing behind", "[tetris][view][fire]")
{
	RunningView fixture;
	TetrisGameState& state = fixture.view.GetManager().GetState();

	// One block on the floor at column 4.
	const uint32_t bottom = state.Height - 1;
	state.Board[bottom * state.Width + 4] = 1;
	fixture.view.Update(1.0f / 60.0f);
	REQUIRE(HasEntityNamed(*fixture.scene, "TetBlock_4_" + std::to_string(bottom)));

	// A fire I piece one row above the floor, spanning the floor block.
	state.CurrentPiece = fixture.view.GetManager().CreatePiece(TetrominoType::I);
	state.CurrentPiece.IsFire = true;
	state.CurrentPiece.Position = glm::ivec2(3, static_cast<int32_t>(bottom) - 1);
	state.UpdatePieceBlocks(state.CurrentPiece);
	for (int i = 0; i < 4; ++i)
	{
		state.CurrentPiece.BlockPos[i] = state.CurrentPiece.Position + state.CurrentPiece.Blocks[i];
		state.CurrentPiece.BlockAlive[i] = true;
	}

	// One gravity step: the fire block drops onto the stack and melts the block
	// it touched.
	fixture.view.Update(1.0f);

	// Let the burn finish - kMeltSeconds is well under a second.
	for (int i = 0; i < 90; ++i)
		fixture.view.Update(1.0f / 60.0f);

	// The melted block is gone: neither its board entity nor its burn animation
	// is left standing on a cell that is no longer on the board.
	REQUIRE_FALSE(HasEntityNamed(*fixture.scene, "TetBlock_4_" + std::to_string(bottom)));
	REQUIRE_FALSE(HasEntityNamed(*fixture.scene, "TetBlockClear_4_" + std::to_string(bottom)));
}

// ---------------------------------------------------------------------------
// The block theme (one OpenGameArt surface per tetromino)
// ---------------------------------------------------------------------------

namespace {

const Forge::PBRMaterialComponent& BlockMaterial(Forge::Scene& scene, const std::string& name)
{
	Forge::Entity entity = scene.FindEntityByName(name);
	REQUIRE(static_cast<bool>(entity));
	REQUIRE(entity.HasComponent<Forge::PBRMaterialComponent>());
	return entity.GetComponent<Forge::PBRMaterialComponent>();
}

// Fills one board cell with a piece and lets the view draw it.
void FillCell(BoardView& view, uint32_t col, uint32_t row, TetrominoType type)
{
	auto& state = view.GetManager().GetState();
	const uint32_t index = static_cast<uint32_t>(row) * state.Width + col;
	REQUIRE(index < state.Board.size());
	state.Board[index] = static_cast<uint8_t>(static_cast<uint8_t>(type) + 1);
	view.Update(0.0f);
}

}  // namespace

TEST_CASE("A locked block is drawn in the colour of the piece that filled it",
	"[tetris][view][theme]")
{
	RunningView fixture;

	const TetrominoType pieces[3] = { TetrominoType::I, TetrominoType::Z, TetrominoType::L };
	for (uint32_t col = 0; col < 3; ++col)
		FillCell(fixture.view, col, 19, pieces[col]);

	// Albedo is white on purpose: pbr.frag multiplies albedo into the texture,
	// so a coloured albedo tints the art and the block ends up in a colour
	// neither the tile nor the theme chose. The piece identity survives as the
	// emissive term, which is additive and so does not fight the texture.
	for (uint32_t col = 0; col < 3; ++col)
	{
		const std::string name = "TetBlock_" + std::to_string(col) + "_19";
		const auto& material = BlockMaterial(*fixture.scene, name);

		REQUIRE(material.Albedo == TetrisManager::GetTetrominoDefinition(pieces[col]).color);
		REQUIRE(material.Emissive == TetrisManager::GetTetrominoDefinition(pieces[col]).color);
		REQUIRE(material.EmissiveIntensity > 0.0f);
		REQUIRE(material.Metallic == kBlockMetallic);
		REQUIRE(material.Roughness == kBlockRoughness);
	}
}

TEST_CASE("A reused piece block takes the surface of the piece it now draws",
	"[tetris][view][theme]")
{
	RunningView fixture;
	auto& state = fixture.view.GetManager().GetState();

	// The four falling blocks are created once and kept for the whole game, so
	// every piece after the first is drawn by entities that arrived wearing
	// somebody else's surface.
	state.CurrentPiece.Type = TetrominoType::S;
	state.UpdatePieceBlocks(state.CurrentPiece);
	fixture.view.Update(0.0f);

	for (uint32_t slot = 0; slot < 4; ++slot)
	{
		REQUIRE(BlockMaterial(*fixture.scene, "TetBlock_Piece" + std::to_string(slot)).Emissive ==
				TetrisManager::GetTetrominoDefinition(TetrominoType::S).color);
	}

	state.CurrentPiece.Type = TetrominoType::J;
	state.UpdatePieceBlocks(state.CurrentPiece);
	fixture.view.Update(0.0f);

	for (uint32_t slot = 0; slot < 4; ++slot)
	{
		REQUIRE(BlockMaterial(*fixture.scene, "TetBlock_Piece" + std::to_string(slot)).Emissive ==
				TetrisManager::GetTetrominoDefinition(TetrominoType::J).color);
	}
}

TEST_CASE("A block a line clear shifts down takes the surface of its new cell",
	"[tetris][view][theme]")
{
	RunningView fixture;
	auto& state = fixture.view.GetManager().GetState();

	// Pin the falling piece out of the way so the counts are the board's alone.
	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	// A full row of Z at the bottom, and a J sitting on top of it.
	for (uint32_t col = 0; col < state.Width; ++col)
		state.Board[state.GetBoardIndex(glm::ivec2(static_cast<int32_t>(col), 19))] =
			static_cast<uint8_t>(static_cast<uint8_t>(TetrominoType::Z) + 1);
	state.Board[state.GetBoardIndex(glm::ivec2(0, 18))] =
		static_cast<uint8_t>(static_cast<uint8_t>(TetrominoType::J) + 1);

	fixture.view.Update(0.0f);
	REQUIRE(BlockMaterial(*fixture.scene, "TetBlock_0_19").Emissive ==
			TetrisManager::GetTetrominoDefinition(TetrominoType::Z).color);

	fixture.view.GetManager().ClearLines();
	fixture.view.Update(0.0f);

	// The J has fallen into cell (0,19). It is the same entity in the same cell,
	// which is exactly why it needs reassigning: a field left out of the update
	// path keeps whatever the cell used to be.
	REQUIRE(BlockMaterial(*fixture.scene, "TetBlock_0_19").Emissive ==
			TetrisManager::GetTetrominoDefinition(TetrominoType::J).color);
	REQUIRE(BlockMaterial(*fixture.scene, "TetBlock_0_19").Emissive ==
			TetrisManager::GetTetrominoDefinition(TetrominoType::J).color);
}

// ---------------------------------------------------------------------------
// The line-clear animation
// ---------------------------------------------------------------------------

namespace {

// How big a block is being drawn, so a test can say what is shrinking.
float BlockScale(Forge::Scene& scene, const std::string& name)
{
	Forge::Entity entity = scene.FindEntityByName(name);
	REQUIRE(static_cast<bool>(entity));
	return entity.GetComponent<Forge::TransformComponent>().Scale.x;
}

// The bottom row filled everywhere except the two cells the O piece drops into,
// and the O pinned at its spawn row: the next hard drop is a line clear whose
// completing blocks are the piece's own, which is the case the animation used
// to lose.
void FillBottomRowExceptO(BoardView& view)
{
	auto& state = view.GetManager().GetState();
	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	for (uint32_t col = 0; col < state.Width; ++col)
	{
		if (col == 4 || col == 5)
			continue;
		state.Board[state.GetBoardIndex(glm::ivec2(static_cast<int32_t>(col), 19))] =
			static_cast<uint8_t>(static_cast<uint8_t>(TetrominoType::I) + 1);
	}
	view.Update(0.0f);
}

}  // namespace

TEST_CASE("Every block of a cleared row animates away, the ones just placed included",
	"[tetris][view][clear]")
{
	RunningView fixture;
	auto& state = fixture.view.GetManager().GetState();
	FillBottomRowExceptO(fixture.view);

	REQUIRE(CountBlockEntities(*fixture.scene) == 12);

	fixture.view.HandleAction(Action::HardDrop, true);
	fixture.view.Update(1.0f / 60.0f);

	REQUIRE(fixture.view.GetLastClearedRows().size() == 1);

	// The row is full and gone in the step the piece locked, so the two cells
	// the player just placed have no board cell to be drawn from, and the
	// cache has no record of them: they were the falling piece. Left out of the
	// animation, they blink out in the frame the line clears - the instant the
	// player is looking at - and the row goes out with eight of its ten blocks.
	for (uint32_t col = 0; col < state.Width; ++col)
	{
		const std::string name = "TetBlockClear_" + std::to_string(col) + "_19";
		INFO(name);
		REQUIRE(HasEntityNamed(*fixture.scene, name));
	}

	// The row's old blocks are already shrinking, and one frame into a 0.25s
	// clear they are not gone: a row that vanishes in one frame is a pop.
	for (uint32_t col = 0; col < state.Width; ++col)
	{
		if (col == 4 || col == 5)
			continue;
		const float scale = BlockScale(*fixture.scene, "TetBlockClear_" + std::to_string(col) + "_19");
		INFO("column " << col << " scale " << scale);
		REQUIRE(scale > 0.0f);
		REQUIRE(scale < 0.92f);
	}

	// The blocks that completed the line start where the piece drew them, above
	// the row, and travel into it. They cannot start at the row: the row is
	// where they are going, not where they were.
	const float rowY = BoardView::CellToWorld(4, 19).y;
	REQUIRE(EntityPosition(*fixture.scene, "TetBlockClear_4_19").y > rowY);
	REQUIRE(EntityPosition(*fixture.scene, "TetBlockClear_5_19").y > rowY);

	// Mid-clear the row is lit: every block of it has its emissive term pulled
	// toward white. A row that only shrinks reads as the blocks getting
	// smaller, not as a line being cleared.
	for (int i = 0; i < 12; ++i)  // 0.2s, near the middle of the 0.25s clear
		fixture.view.Update(1.0f / 60.0f);

	const glm::vec3 pieceColor = TetrisManager::GetTetrominoDefinition(TetrominoType::I).color;
	for (uint32_t col = 0; col < state.Width; ++col)
	{
		if (col == 4 || col == 5)
			continue;
		const glm::vec3 lit = BlockMaterial(*fixture.scene, "TetBlockClear_" + std::to_string(col) + "_19").Emissive;
		INFO("column " << col << " emissive " << lit.x << "," << lit.y << "," << lit.z);
		REQUIRE(lit.x > pieceColor.x);
		REQUIRE(lit.y > pieceColor.y);
		REQUIRE(lit.z > pieceColor.z);
	}

	// The placed blocks light up once they have landed and their own clear is
	// under way: they travel into the row first, and their flash cannot start
	// before the row they filled begins to go.
	for (int i = 0; i < 10; ++i)
		fixture.view.Update(1.0f / 60.0f);

	for (uint32_t col = 4; col <= 5; ++col)
	{
		const glm::vec3 lit = BlockMaterial(*fixture.scene, "TetBlockClear_" + std::to_string(col) + "_19").Emissive;
		INFO("placed block at column " << col);
		REQUIRE(lit.x > pieceColor.x);
		REQUIRE(lit.y > pieceColor.y);
		REQUIRE(lit.z > pieceColor.z);
	}

	// The O's upper cells drop into the bottom row, drawn by the entities that
	// were drawing them: the cache follows the block, not the cell.
	REQUIRE(HasEntityNamed(*fixture.scene, "TetBlock_4_19"));
	REQUIRE(CountBlockEntities(*fixture.scene) == 6);

	// After the clear the row's blocks are gone, and the stack has arrived
	// exactly where the rules put it.
	for (int i = 0; i < 120; ++i)
		fixture.view.Update(1.0f / 60.0f);

	REQUIRE_FALSE(HasEntityNamed(*fixture.scene, "TetBlockClear_4_19"));
	REQUIRE_FALSE(HasEntityNamed(*fixture.scene, "TetBlockClear_0_19"));
	REQUIRE(CountBlockEntities(*fixture.scene) == 6);
	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_4_19") == BoardView::CellToWorld(4, 19));
	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_5_19") == BoardView::CellToWorld(5, 19));
}

TEST_CASE("The stack above a cleared row keeps its blocks and falls after the row is gone",
	"[tetris][view][clear]")
{
	RunningView fixture;
	auto& state = fixture.view.GetManager().GetState();
	FillBottomRowExceptO(fixture.view);

	// One block sitting on the row that is about to clear.
	state.Board[state.GetBoardIndex(glm::ivec2(0, 18))] =
		static_cast<uint8_t>(static_cast<uint8_t>(TetrominoType::J) + 1);
	fixture.view.Update(0.0f);

	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_0_18") == BoardView::CellToWorld(0, 18));

	fixture.view.HandleAction(Action::HardDrop, true);
	fixture.view.Update(1.0f / 60.0f);

	// Board row 18's J is board row 19's now. Keyed by cell, the entity was
	// found under its old row, the new row found nothing, and the stack above a
	// cleared row arrived in one frame - which is what the motion that measures
	// a block against the row it was written in was written to prevent, and
	// could never reach.
	REQUIRE(HasEntityNamed(*fixture.scene, "TetBlock_0_19"));
	REQUIRE_FALSE(HasEntityNamed(*fixture.scene, "TetBlock_0_18"));
	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_0_19").y ==
			Catch::Approx(BoardView::CellToWorld(0, 18).y).margin(1e-3f));

	// It is standing on its old row, because the row it falls into is still
	// animating away. A block sliding through a shrinking row is two
	// animations in one cell, and the clear reads as the stack moving through a
	// fading wall.
	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_0_19").y ==
			Catch::Approx(BoardView::CellToWorld(0, 18).y).margin(1e-3f));

	for (int i = 0; i < 13; ++i)  // 0.217s, inside the 0.25s clear
		fixture.view.Update(1.0f / 60.0f);
	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_0_19").y ==
			Catch::Approx(BoardView::CellToWorld(0, 18).y).margin(1e-3f));
	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_0_19").x ==
			Catch::Approx(BoardView::CellToWorld(0, 18).x).margin(1e-3f));

	// Then it falls, and lands exactly where the rules say it is.
	for (int i = 0; i < 60; ++i)
		fixture.view.Update(1.0f / 60.0f);
	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_0_19") == BoardView::CellToWorld(0, 19));
}

TEST_CASE("Clearing two lines at once animates both rows", "[tetris][view][clear]")
{
	RunningView fixture;
	auto& state = fixture.view.GetManager().GetState();

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	for (uint32_t row = 18; row <= 19; ++row)
	{
		for (uint32_t col = 0; col < state.Width; ++col)
		{
			if (col == 4 || col == 5)
				continue;
			state.Board[state.GetBoardIndex(glm::ivec2(static_cast<int32_t>(col), static_cast<int32_t>(row)))] =
				static_cast<uint8_t>(static_cast<uint8_t>(TetrominoType::I) + 1);
		}
	}
	fixture.view.Update(0.0f);
	REQUIRE(CountBlockEntities(*fixture.scene) == 20);

	fixture.view.HandleAction(Action::HardDrop, true);
	fixture.view.Update(1.0f / 60.0f);

	REQUIRE(fixture.view.GetLastClearedRows().size() == 2);

	// Both rows, every cell of both, animating: the four the piece placed, two
	// in each row, included.
	for (uint32_t row = 18; row <= 19; ++row)
	{
		for (uint32_t col = 0; col < state.Width; ++col)
		{
			const std::string name = "TetBlockClear_" + std::to_string(col) + "_" + std::to_string(row);
			INFO(name);
			REQUIRE(HasEntityNamed(*fixture.scene, name));
		}
	}

	for (int i = 0; i < 120; ++i)
		fixture.view.Update(1.0f / 60.0f);

	// Both rows gone, the board empty, only the falling piece drawn.
	REQUIRE(CountBlockEntities(*fixture.scene) == 4);
}

// ---------------------------------------------------------------------------
// Hard drop compaction
// ---------------------------------------------------------------------------

TEST_CASE("A hard drop slides the block it packs down, and keeps its entity",
	"[tetris][view][compaction]")
{
	RunningView fixture;
	auto& state = fixture.view.GetManager().GetState();

	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);

	// A block with nine rows of nothing under it, in the column the piece lands
	// on: the impact travels down through it, finds the gap, and the gap closes.
	state.Board[state.GetBoardIndex(glm::ivec2(4, 10))] =
		static_cast<uint8_t>(static_cast<uint8_t>(TetrominoType::J) + 1);
	fixture.view.Update(0.0f);

	REQUIRE(CountBlockEntities(*fixture.scene) == 5);
	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_4_10") == BoardView::CellToWorld(4, 10));

	fixture.view.HandleAction(Action::HardDrop, true);
	fixture.view.Update(1.0f / 60.0f);

	// The block the player dropped nine rows ago is on the floor now, under a
	// new name, and it is the same entity: keyed by cell, the old key is gone
	// and the new one was never in the cache, which is what the relabel
	// exists for. Destroying and rebuilding it here would be a teleport.
	REQUIRE(HasEntityNamed(*fixture.scene, "TetBlock_4_19"));
	REQUIRE_FALSE(HasEntityNamed(*fixture.scene, "TetBlock_4_10"));
	// The packed block, the four the drop placed, and the four of the piece
	// that spawned after it: nothing destroyed and rebuilt on the way down.
	REQUIRE(CountBlockEntities(*fixture.scene) == 9);

	// On the frame of the drop it is still standing on the row it was drawn on:
	// the block the player just dropped has to come to rest before the stack
	// beneath it closes.
	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_4_19").y ==
			Catch::Approx(BoardView::CellToWorld(4, 10).y).margin(1e-3f));

	for (int i = 0; i < 60; ++i)
		fixture.view.Update(1.0f / 60.0f);

	REQUIRE(EntityPosition(*fixture.scene, "TetBlock_4_19") == BoardView::CellToWorld(4, 19));
}

TEST_CASE("A hard drop clears the row its compaction completes", "[tetris][view][compaction][clear]")
{
	RunningView fixture;
	auto& state = fixture.view.GetManager().GetState();

	// The bottom row has a hole in column 4, and the stack resting on a gap
	// above it is out of any piece's reach. The impact travels down that stack,
	// closes the gap and the stack lands in the hole - completing the row.
	// Measuring fullness on the unpacked board reports no clear at all: no
	// flash, no reward, and a stack that leaves the row the compaction
	// completed standing in mid air.
	//
	// The whole board is written before the view's next Update: a board the
	// view has never seen is a board it cannot mistake for a lock, and a lock
	// it invents here animates the row away before the drop that completes it.
	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);
	for (uint32_t col = 0; col < state.Width; ++col)
	{
		if (col == 4)
			continue;
		state.Board[state.GetBoardIndex(glm::ivec2(static_cast<int32_t>(col), 19))] =
			static_cast<uint8_t>(static_cast<uint8_t>(TetrominoType::I) + 1);
	}
	state.Board[state.GetBoardIndex(glm::ivec2(4, 16))] =
		static_cast<uint8_t>(static_cast<uint8_t>(TetrominoType::J) + 1);
	state.Board[state.GetBoardIndex(glm::ivec2(4, 17))] =
		static_cast<uint8_t>(static_cast<uint8_t>(TetrominoType::J) + 1);
	fixture.view.Update(0.0f);

	fixture.view.HandleAction(Action::HardDrop, true);
	fixture.view.Update(1.0f / 60.0f);

	REQUIRE(fixture.view.GetLastClearedRows().size() == 1);

	// Every cell of the row the pack completed is animating out.
	for (uint32_t col = 0; col < state.Width; ++col)
	{
		const std::string name = "TetBlockClear_" + std::to_string(col) + "_19";
		INFO(name);
		REQUIRE(HasEntityNamed(*fixture.scene, name));
	}

	// The row waits for the pack: a row flashing out while the block that
	// completed it is still travelling is the reward gone before the player
	// sees it land.
	REQUIRE(EntityPosition(*fixture.scene, "TetBlockClear_0_19").y ==
			Catch::Approx(BoardView::CellToWorld(0, 19).y).margin(1e-3f));

	for (int i = 0; i < 70; ++i)
		fixture.view.Update(1.0f / 60.0f);

	// The row is gone, and the piece that travelled down with the closing stack
	// dropped into the row it filled.
	REQUIRE_FALSE(HasEntityNamed(*fixture.scene, "TetBlockClear_0_19"));
	REQUIRE(HasEntityNamed(*fixture.scene, "TetBlock_4_19"));
	REQUIRE(HasEntityNamed(*fixture.scene, "TetBlock_4_18"));
	REQUIRE(HasEntityNamed(*fixture.scene, "TetBlock_4_17"));
	REQUIRE_FALSE(HasEntityNamed(*fixture.scene, "TetBlock_4_16"));

	for (uint32_t row = 0; row < state.Height; ++row)
	{
		for (uint32_t col = 0; col < state.Width; ++col)
		{
			const uint32_t index = row * state.Width + col;
			const std::string name = "TetBlock_" + std::to_string(col) + "_" + std::to_string(row);
			INFO(name);
			if (index < state.Board.size() && state.Board[index] != 0)
			{
				REQUIRE(HasEntityNamed(*fixture.scene, name));
				REQUIRE(EntityPosition(*fixture.scene, name) == BoardView::CellToWorld(col, row));
			}
			else
			{
				REQUIRE_FALSE(HasEntityNamed(*fixture.scene, name));
			}
		}
	}
}

TEST_CASE("Impact measures the pack as distance the landing blocks slid",
	"[tetris][view][compaction]")
{
	RunningView fixture;
	auto& state = fixture.view.GetManager().GetState();

	// A packed board: the drop lands on it and nothing moves.
	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);
	fixture.view.Update(0.0f);
	fixture.view.HandleAction(Action::HardDrop, true);
	fixture.view.Update(1.0f / 60.0f);
	REQUIRE(fixture.view.GetLastImpact().Occurred);
	REQUIRE(fixture.view.GetLastImpact().Compact == Catch::Approx(0.0f));

	// The same drop with a hole under the column it lands in: the blocks come
	// to rest below the cells they locked into, and a strike measured against
	// the landing cell stops short of where the block ends up. The pack closes
	// the stack first, then the piece travels as one body, so all four of its
	// blocks slide the same 7 rows - the row where any one of them would not
	// fit is where the whole piece stops.
	state.CurrentPiece.Type = TetrominoType::O;
	state.CurrentPiece.Position = glm::ivec2(4, 0);
	state.UpdatePieceBlocks(state.CurrentPiece);
	state.Board[state.GetBoardIndex(glm::ivec2(4, 10))] =
		static_cast<uint8_t>(static_cast<uint8_t>(TetrominoType::J) + 1);
	fixture.view.Update(0.0f);

	fixture.view.HandleAction(Action::HardDrop, true);
	fixture.view.Update(1.0f / 60.0f);
	REQUIRE(fixture.view.GetLastImpact().Occurred);
	REQUIRE(fixture.view.GetLastImpact().Compact == Catch::Approx(7.0f));
}

