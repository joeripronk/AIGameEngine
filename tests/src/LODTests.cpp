#include <catch2/catch_test_macros.hpp>

#include "Forge/Renderer/LOD.h"

using namespace Forge;

TEST_CASE("A mesh with no levels is drawn in full detail", "[renderer][lod]")
{
	// No switch distances: one level, and it is the one that is drawn.
	REQUIRE(LOD::ChooseLevel({}, 0.0f) == 0);
	REQUIRE(LOD::ChooseLevel({}, 1000.0f) == 0);
}

TEST_CASE("A distant mesh is drawn with fewer triangles", "[renderer][lod]")
{
	// The first distance is where the second level takes over, the second
	// where the third does.
	const std::vector<float> distances = { 10.0f, 30.0f };

	// Close: the most detailed level.
	REQUIRE(LOD::ChooseLevel(distances, 0.0f) == 0);
	REQUIRE(LOD::ChooseLevel(distances, 9.9f) == 0);

	// In between: the second level.
	REQUIRE(LOD::ChooseLevel(distances, 10.0f) == 1);
	REQUIRE(LOD::ChooseLevel(distances, 29.9f) == 1);

	// Far: the least detailed level.
	REQUIRE(LOD::ChooseLevel(distances, 30.0f) == 2);
	REQUIRE(LOD::ChooseLevel(distances, 1000.0f) == 2);
}

TEST_CASE("The level is never beyond the levels there are", "[renderer][lod]")
{
	// More switch distances than levels: the last level is the one that is
	// drawn, and the choice does not run off the end.
	const std::vector<float> distances = { 10.0f, 20.0f, 30.0f, 40.0f };
	REQUIRE(LOD::ChooseLevel(distances, 1000.0f) == 4);
}
