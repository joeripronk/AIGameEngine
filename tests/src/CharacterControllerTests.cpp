#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Forge/Physics/CharacterController.h"
#include "Forge/Physics/Physics.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"

using namespace Forge;

namespace {

	// A floor: a box under the origin.
	CharacterObstacle MakeFloor(float top = 0.0f)
	{
		CharacterObstacle obstacle;
		obstacle.Min = glm::vec3(-100.0f, top - 1.0f, -100.0f);
		obstacle.Max = glm::vec3(100.0f, top, 100.0f);
		return obstacle;
	}

	// A wall at x, from the floor up.
	CharacterObstacle MakeWall(float x, float height = 3.0f)
	{
		CharacterObstacle obstacle;
		obstacle.Min = glm::vec3(x, 0.0f, -100.0f);
		obstacle.Max = glm::vec3(x + 0.5f, height, 100.0f);
		return obstacle;
	}

}  // namespace

TEST_CASE("A character walks", "[physics][character]")
{
	const CharacterShape shape;
	const CharacterSettings settings;
	const std::vector<CharacterObstacle> obstacles;

	const CharacterMoveResult result = Character::Move(
		glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), shape, settings, obstacles);

	REQUIRE(result.Position == glm::vec3(1.0f, 0.0f, 0.0f));
	REQUIRE_FALSE(result.OnWall);
}

TEST_CASE("A character is stopped by a wall", "[physics][character]")
{
	const CharacterShape shape;
	const CharacterSettings settings;
	const std::vector<CharacterObstacle> obstacles{ MakeWall(2.0f) };

	// The character walks into the wall at x = 2 and stops in front of it:
	// its own radius is 0.4, so it ends at 1.6.
	const CharacterMoveResult result = Character::Move(
		glm::vec3(0.0f), glm::vec3(5.0f, 0.0f, 0.0f), shape, settings, obstacles);

	REQUIRE(result.OnWall);
	REQUIRE(result.WallNormal == glm::vec3(-1.0f, 0.0f, 0.0f));
	REQUIRE(result.Position.x == Catch::Approx(1.6f));
}

TEST_CASE("A character slides along a wall", "[physics][character]")
{
	const CharacterShape shape;
	const CharacterSettings settings;
	const std::vector<CharacterObstacle> obstacles{ MakeWall(2.0f) };

	// The character is stopped on the x axis and still moves on the z one:
	// that is what sliding along a wall is.
	const CharacterMoveResult result = Character::Move(
		glm::vec3(0.0f), glm::vec3(5.0f, 0.0f, 3.0f), shape, settings, obstacles);

	REQUIRE(result.OnWall);
	REQUIRE(result.Position.x == Catch::Approx(1.6f));
	REQUIRE(result.Position.z == Catch::Approx(3.0f));
}

TEST_CASE("A character climbs a step", "[physics][character]")
{
	const CharacterShape shape;
	CharacterSettings settings;
	settings.StepHeight = 0.4f;

	// A kerb: 0.3 high, which is within the step height.
	const std::vector<CharacterObstacle> obstacles{ MakeFloor(0.0f), MakeWall(2.0f, 0.3f) };

	const CharacterMoveResult result = Character::Move(
		glm::vec3(0.0f), glm::vec3(5.0f, 0.0f, 0.0f), shape, settings, obstacles);

	// The character is on top of the kerb, and it got past it.
	REQUIRE(result.Position.y == Catch::Approx(0.3f));
	REQUIRE(result.Position.x > 2.0f);
}

TEST_CASE("A character does not climb a wall", "[physics][character]")
{
	const CharacterShape shape;
	CharacterSettings settings;
	settings.StepHeight = 0.4f;

	// A wall two metres high: no step height gets over it.
	const std::vector<CharacterObstacle> obstacles{ MakeFloor(0.0f), MakeWall(2.0f, 2.0f) };

	const CharacterMoveResult result = Character::Move(
		glm::vec3(0.0f), glm::vec3(5.0f, 0.0f, 0.0f), shape, settings, obstacles);

	REQUIRE(result.OnWall);
	REQUIRE(result.Position.y == Catch::Approx(0.0f));
	REQUIRE(result.Position.x == Catch::Approx(1.6f));
}

TEST_CASE("A character lands on the floor", "[physics][character]")
{
	const CharacterShape shape;
	const CharacterSettings settings;
	const std::vector<CharacterObstacle> obstacles{ MakeFloor(0.0f) };

	// The character is above the floor and falls onto it.
	const CharacterMoveResult result = Character::Move(
		glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, -5.0f, 0.0f), shape, settings, obstacles);

	REQUIRE(result.OnFloor);
	REQUIRE(result.Position.y == Catch::Approx(0.0f));
}

TEST_CASE("A character's head hits the ceiling", "[physics][character]")
{
	const CharacterShape shape;
	const CharacterSettings settings;

	CharacterObstacle ceiling;
	ceiling.Min = glm::vec3(-100.0f, 1.0f, -100.0f);
	ceiling.Max = glm::vec3(100.0f, 2.0f, 100.0f);
	const std::vector<CharacterObstacle> obstacles{ ceiling };

	// The character jumps and hits the ceiling: it stops with its head
	// against it, at the height where its head is.
	const CharacterMoveResult result = Character::Move(
		glm::vec3(0.0f), glm::vec3(0.0f, 5.0f, 0.0f), shape, settings, obstacles);

	REQUIRE(result.OnCeiling);
	REQUIRE(result.Position.y == Catch::Approx(1.0f - shape.Height));
}

TEST_CASE("A character knows when it is on the floor", "[physics][character]")
{
	const CharacterShape shape;
	const CharacterSettings settings;
	const std::vector<CharacterObstacle> obstacles{ MakeFloor(0.0f) };

	// Standing on the floor.
	REQUIRE(Character::IsOnFloor(glm::vec3(0.0f), shape, settings, obstacles));

	// In the air.
	REQUIRE_FALSE(Character::IsOnFloor(glm::vec3(0.0f, 1.0f, 0.0f), shape, settings, obstacles));

	// Beside the floor, not on it.
	REQUIRE_FALSE(Character::IsOnFloor(glm::vec3(0.0f, 0.0f, 500.0f), shape, settings, obstacles));
}

TEST_CASE("A character component walks and lands", "[physics][character]")
{
	Scene scene;

	// A floor the character stands on.
	{
		Entity floor = scene.CreateEntity("Floor");
		floor.GetComponent<TransformComponent>().Translation = { 0.0f, -0.5f, 0.0f };
		auto& collider = floor.AddComponent<BoxColliderComponent>();
		collider.Size = { 10.0f, 1.0f, 10.0f };
		// The world's bodies are the ones with a rigidbody: a static one is
		// the floor a character walks on.
		floor.AddComponent<RigidbodyComponent>().Type = RigidbodyComponent::BodyType::Static;
	}

	// The character, in the air.
	Entity character = scene.CreateEntity("Character");
	character.GetComponent<TransformComponent>().Translation = { 0.0f, 3.0f, 0.0f };

	auto& controller = character.AddComponent<CharacterControllerComponent>();
	controller.Shape.Radius = 0.4f;
	controller.Shape.Height = 1.8f;

	// The physics has to be running for the world's boxes to exist.
	Physics::Init();
	scene.OnPhysicsStart();

	// A few frames of falling: the character lands on the floor.
	for (int i = 0; i < 120; ++i)
		scene.UpdateCharacterControllers(Timestep(1.0f / 60.0f));

	REQUIRE(controller.OnFloor);
	REQUIRE(character.GetComponent<TransformComponent>().Translation.y == Catch::Approx(0.0f).margin(0.01f));

	// And it walks: the game says how fast, and the character moves.
	controller.Velocity = { 2.0f, 0.0f, 0.0f };
	for (int i = 0; i < 30; ++i)
		scene.UpdateCharacterControllers(Timestep(1.0f / 60.0f));

	REQUIRE(character.GetComponent<TransformComponent>().Translation.x == Catch::Approx(1.0f).margin(0.05f));

	scene.OnPhysicsStop();
	Physics::Shutdown();
}

TEST_CASE("The world's boxes are the character's obstacles", "[physics][character]")
{
	const std::vector<glm::vec3> minima{ glm::vec3(-1.0f, 0.0f, -1.0f), glm::vec3(5.0f, 0.0f, -1.0f) };
	const std::vector<glm::vec3> maxima{ glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(6.0f, 1.0f, 1.0f) };

	const std::vector<CharacterObstacle> obstacles = Character::CollectObstacles(minima, maxima);
	REQUIRE(obstacles.size() == 2);
	REQUIRE(obstacles[0].Min == minima[0]);
	REQUIRE(obstacles[1].Max == maxima[1]);
}
