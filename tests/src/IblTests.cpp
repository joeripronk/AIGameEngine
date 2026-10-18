#include <catch2/catch_test_macros.hpp>

#include "Forge/Renderer/IBL.h"

TEST_CASE("IBL: env mip selection stays within the map's level count", "[renderer][ibl]")
{
	// The engine's cube maps carry a single level, so the only legal LOD is 0.
	REQUIRE(Forge::SelectEnvMip(0.0f, 1u) == 0.0f);
	REQUIRE(Forge::SelectEnvMip(0.5f, 1u) == 0.0f);
	REQUIRE(Forge::SelectEnvMip(1.0f, 1u) == 0.0f);

	// With a real chain the rule still never exceeds the last level.
	REQUIRE(Forge::SelectEnvMip(1.0f, 6u) <= 5.0f);
}

TEST_CASE("IBL: specular blur weight is clamped to [0,1]", "[renderer][ibl]")
{
	REQUIRE(Forge::EnvSpecularBlur(-1.0f) == 0.0f);
	REQUIRE(Forge::EnvSpecularBlur(0.0f) == 0.0f);
	REQUIRE(Forge::EnvSpecularBlur(0.5f) == 0.5f);
	REQUIRE(Forge::EnvSpecularBlur(1.0f) == 1.0f);
	REQUIRE(Forge::EnvSpecularBlur(2.0f) == 1.0f);
}
