#include <catch2/catch_test_macros.hpp>

#include "Forge/Scripting/ScriptEngine.h"

TEST_CASE("ScriptEngine initializes", "[scripting]")
{
    REQUIRE_NOTHROW(Forge::ScriptEngine::Init());
}

TEST_CASE("ScriptEngine shuts down", "[scripting]")
{
    Forge::ScriptEngine::Init();
    REQUIRE_NOTHROW(Forge::ScriptEngine::Shutdown());
}
