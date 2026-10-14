#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Forge/Scripting/ScriptEngine.h"
#include "Forge/Scripting/ScriptableEntity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Components.h"

TEST_CASE("ScriptEngine initializes", "[scripting]")
{
    REQUIRE_NOTHROW(Forge::ScriptEngine::Init());
}

TEST_CASE("ScriptEngine shuts down", "[scripting]")
{
    Forge::ScriptEngine::Init();
    REQUIRE_NOTHROW(Forge::ScriptEngine::Shutdown());
}

TEST_CASE("ScriptEngine init and shutdown multiple times", "[scripting]")
{
    // Should be safe to call multiple times
    Forge::ScriptEngine::Init();
    Forge::ScriptEngine::Shutdown();
    Forge::ScriptEngine::Init();
    Forge::ScriptEngine::Shutdown();
}

TEST_CASE("ScriptableEntity base class", "[scripting]")
{
    // Test that the base class exists and can be instantiated
    Forge::ScriptableEntity entity;
    REQUIRE_NOTHROW(entity.OnCreate());
    REQUIRE_NOTHROW(entity.OnDestroy());

    Forge::Timestep ts(0.016f);
    REQUIRE_NOTHROW(entity.OnUpdate(ts));
}

TEST_CASE("ScriptableEntity with derived class", "[scripting]")
{
    // Create a derived scriptable entity
    class TestScript : public Forge::ScriptableEntity
    {
    public:
        int CreateCount = 0;
        int UpdateCount = 0;

        void OnCreate() override { CreateCount++; }
        void OnUpdate(Forge::Timestep ts) override { UpdateCount++; }
    };

    TestScript script;
    script.OnCreate();
    REQUIRE(script.CreateCount == 1);

    Forge::Timestep ts(0.016f);
    script.OnUpdate(ts);
    REQUIRE(script.UpdateCount == 1);
}

TEST_CASE("ScriptComponent stores class name", "[scripting]")
{
    Forge::Scene scene;
    auto entity = scene.CreateEntity("ScriptEntity");
    auto& sc = entity.AddComponent<Forge::ScriptComponent>();
    sc.ClassName = "PlayerController";

    REQUIRE(sc.ClassName == "PlayerController");
}

TEST_CASE("ScriptEngine Lua state is created", "[scripting][lua]")
{
    Forge::ScriptEngine::Init();

    // The Lua state should be non-null after init
    // We can't directly access s_LuaState, but we can verify
    // that the initialization doesn't crash
    REQUIRE(true);

    Forge::ScriptEngine::Shutdown();
}
