#include <catch2/catch_test_macros.hpp>

#include "Forge/Physics/Physics.h"

TEST_CASE("Physics initializes", "[physics]")
{
    REQUIRE_NOTHROW(Forge::Physics::Init());
}

TEST_CASE("Physics shuts down", "[physics]")
{
    Forge::Physics::Init();
    REQUIRE_NOTHROW(Forge::Physics::Shutdown());
}
