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

TEST_CASE("The preview lays the queued piece out the way it spawns", "[tetris][view]")
{
	// The tray is 4x4 blocks, so a cell centre may sit no further out than
	// 1.5 - a block half a unit wide then stays inside the tray.
	constexpr float kMaxExtent = 1.5f;

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
		for (const glm::vec3& offset : offsets)
			shown.insert({ static_cast<int>(std::lround(offset.x - minX)),
				static_cast<int>(std::lround(maxY - offset.y)) });

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
	REQUIRE(mid.x > before.x - 1.0f);
	REQUIRE(mid.y == Catch::Approx(before.y));

	for (int i = 0; i < 30; ++i)
		fixture.view.Update(1.0f / 60.0f);

	// The ease ends where the board says it does, exactly.
	const glm::vec3 settled = EntityPosition(*fixture.scene, "TetBlock_Piece0");
	REQUIRE(settled.x == Catch::Approx(before.x - 1.0f));
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
	REQUIRE(ys[start] - ys[end] == Catch::Approx(1.0f).margin(0.001f));
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

TEST_CASE("The theme gives every tetromino its own surface", "[tetris][view][theme]")
{
	std::set<std::string> seen;
	for (uint8_t i = 0; i < static_cast<uint8_t>(TetrominoType::Count); ++i)
	{
		const char* texture = GetBlockTexture(static_cast<TetrominoType>(i));
		REQUIRE(texture != nullptr);
		// A path the engine can actually open, and one per piece: seven pieces
		// sharing two surfaces is six pieces that are not distinguishable.
		REQUIRE(std::string(texture).rfind("resources/textures/tetris/block_", 0) == 0);
		REQUIRE(seen.insert(texture).second == true);
	}

	// Out of range answers with a surface, not with a read past the end of the
	// table. `TetrominoType::Count` is a real value - an unassigned piece.
	REQUIRE(std::string(GetBlockTexture(TetrominoType::Count)) ==
			std::string(GetBlockTexture(TetrominoType::I)));
}

TEST_CASE("A locked block is drawn in the surface of the piece that filled it",
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

		REQUIRE(material.AlbedoTexturePath == GetBlockTexture(pieces[col]));
		REQUIRE(material.Albedo == glm::vec3(1.0f, 1.0f, 1.0f));
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
		REQUIRE(BlockMaterial(*fixture.scene, "TetBlock_Piece" + std::to_string(slot)).AlbedoTexturePath ==
				GetBlockTexture(TetrominoType::S));
	}

	state.CurrentPiece.Type = TetrominoType::J;
	state.UpdatePieceBlocks(state.CurrentPiece);
	fixture.view.Update(0.0f);

	for (uint32_t slot = 0; slot < 4; ++slot)
	{
		REQUIRE(BlockMaterial(*fixture.scene, "TetBlock_Piece" + std::to_string(slot)).AlbedoTexturePath ==
				GetBlockTexture(TetrominoType::J));
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
	REQUIRE(BlockMaterial(*fixture.scene, "TetBlock_0_19").AlbedoTexturePath ==
			GetBlockTexture(TetrominoType::Z));

	fixture.view.GetManager().ClearLines();
	fixture.view.Update(0.0f);

	// The J has fallen into cell (0,19). It is the same entity in the same cell,
	// which is exactly why it needs reassigning: a field left out of the update
	// path keeps whatever the cell used to be.
	REQUIRE(BlockMaterial(*fixture.scene, "TetBlock_0_19").AlbedoTexturePath ==
			GetBlockTexture(TetrominoType::J));
	REQUIRE(BlockMaterial(*fixture.scene, "TetBlock_0_19").Emissive ==
			TetrisManager::GetTetrominoDefinition(TetrominoType::J).color);
}
