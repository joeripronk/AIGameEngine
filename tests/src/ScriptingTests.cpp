#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
using Catch::Approx;

#include "Forge/Scripting/ScriptEngine.h"
#include "Forge/Scripting/ScriptableEntity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Components.h"
#include "Forge/Core/Log.h"

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
        void OnUpdate(Forge::Timestep ts) override { (void)ts; UpdateCount++; }
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

// ---------------------------------------------------------------------------
// Physics collisions delivered into Lua
// ---------------------------------------------------------------------------

#include "Forge/Physics/Physics.h"

namespace {
    // Other suites point the script directory at a temp folder and leave it
    // there, so tests that need the shipped fixtures must set it themselves
    // rather than rely on run order.
    const char* kScriptDir = FORGE_SOURCE_DIR "/resources/scripts/";
}

TEST_CASE("Lua script receives collision begin and end events", "[scripting][collision]")
{
    Forge::Log::Init();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::ScriptEngine::SetScriptDirectory(kScriptDir);

    Forge::Scene scene;

    // Static ground.
    auto ground = scene.CreateEntity("Ground");
    auto& groundRb = ground.AddComponent<Forge::RigidbodyComponent>();
    groundRb.Type = Forge::RigidbodyComponent::BodyType::Static;
    auto& groundCollider = ground.AddComponent<Forge::BoxColliderComponent>();
    groundCollider.Size = glm::vec3(10.0f, 1.0f, 10.0f);
    ground.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f);

    // Dynamic ball carrying the Collider script.
    auto ball = scene.CreateEntity("Ball");
    auto& ballRb = ball.AddComponent<Forge::RigidbodyComponent>();
    ballRb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    auto& ballCollider = ball.AddComponent<Forge::BoxColliderComponent>();
    ballCollider.Size = glm::vec3(1.0f);
    auto& ballScript = ball.AddComponent<Forge::ScriptComponent>();
    ballScript.ClassName = "Collider";
    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 5.0f, 0.0f);

    Forge::ScriptEngine::OnRuntimeStart(&scene);

    double hits = -1.0;
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(ball, "Hits", hits));
    REQUIRE(hits == Approx(0.0));

    scene.OnPhysicsStart();

    // Still clear of the ground.
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(ball, "Hits", hits));
    REQUIRE(hits == Approx(0.0));

    // Drop onto the ground: OnCollision must fire.
    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 0.6f, 0.0f);
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));

    REQUIRE(Forge::ScriptEngine::GetScriptNumber(ball, "Hits", hits));
    REQUIRE(hits == Approx(1.0));

    // The script saw the partner entity, not nil.
    std::string otherName;
    REQUIRE(Forge::ScriptEngine::GetScriptString(ball, "LastOtherName", otherName));
    REQUIRE(otherName == "Ground");

    // Pull away: OnCollisionEnd must fire.
    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 5.0f, 0.0f);
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));

    double separations = -1.0;
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(ball, "Separations", separations));
    REQUIRE(separations == Approx(1.0));

    scene.OnPhysicsStop();
    Forge::ScriptEngine::OnRuntimeStop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Script without collision handlers is unaffected by contacts", "[scripting][collision]")
{
    Forge::Log::Init();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::ScriptEngine::SetScriptDirectory(kScriptDir);

    Forge::Scene scene;

    auto ground = scene.CreateEntity("Ground");
    auto& groundRb = ground.AddComponent<Forge::RigidbodyComponent>();
    groundRb.Type = Forge::RigidbodyComponent::BodyType::Static;
    auto& groundCollider = ground.AddComponent<Forge::BoxColliderComponent>();
    groundCollider.Size = glm::vec3(10.0f, 1.0f, 10.0f);

    // Mover.lua defines no OnCollision; the bridge must tolerate that silently.
    auto ball = scene.CreateEntity("Mover");
    auto& ballRb = ball.AddComponent<Forge::RigidbodyComponent>();
    ballRb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    auto& ballCollider = ball.AddComponent<Forge::BoxColliderComponent>();
    ballCollider.Size = glm::vec3(1.0f);
    auto& ballScript = ball.AddComponent<Forge::ScriptComponent>();
    ballScript.ClassName = "Mover";
    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 5.0f, 0.0f);

    Forge::ScriptEngine::OnRuntimeStart(&scene);
    scene.OnPhysicsStart();

    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 0.6f, 0.0f);
    REQUIRE_NOTHROW(scene.OnUpdateSimulation(Forge::Timestep(0.016f)));

    scene.OnPhysicsStop();
    Forge::ScriptEngine::OnRuntimeStop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Script field access rejects entities without scripts", "[scripting]")
{
    Forge::Log::Init();
    Forge::ScriptEngine::Init();

    Forge::Scene scene;
    auto entity = scene.CreateEntity("NoScript");

    double value = 0.0;
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(entity, "Anything", value) == false);

    std::string text;
    REQUIRE(Forge::ScriptEngine::GetScriptString(entity, "Anything", text) == false);
    REQUIRE(Forge::ScriptEngine::SetScriptNumber(entity, "Anything", 1.0) == false);
}

// ---------------------------------------------------------------------------
// Mutating bindings report failure
// ---------------------------------------------------------------------------

TEST_CASE("Entity setters report success and failure", "[scripting][setters]")
{
    Forge::Log::Init();
    Forge::Scene scene;

    const char* kScriptDir = FORGE_SOURCE_DIR "/resources/scripts/";
    Forge::ScriptEngine::SetScriptDirectory(kScriptDir);

    auto entity = scene.CreateEntity("Setter");
    auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
    rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    auto& collider = entity.AddComponent<Forge::BoxColliderComponent>();
    collider.Size = glm::vec3(1.0f);
    auto& script = entity.AddComponent<Forge::ScriptComponent>();
    script.ClassName = "SetterReporting";

    Forge::ScriptEngine::OnRuntimeStart(&scene);

    bool flag = false;

    // A valid component and field must report true, and must actually apply.
    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "SetFloatOK", flag));
    REQUIRE(flag == true);

    // Read back inside the script, because the legacy calls below overwrite
    // Friction later in the same OnCreate.
    double frictionAfterGoodSet = -1.0;
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(entity, "FrictionAfterGoodSet", frictionAfterGoodSet));
    REQUIRE(frictionAfterGoodSet == Approx(0.5));

    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "SetBoolOK", flag));
    REQUIRE(flag == true);
    REQUIRE(rb.FixedRotation == true);

    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "SetVector3OK", flag));
    REQUIRE(flag == true);

    // Also read inside the script: the legacy SetPosition below overwrites it.
    double translationAfterGoodSet = -1.0;
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(entity, "TranslationAfterGoodSet", translationAfterGoodSet));
    REQUIRE(translationAfterGoodSet == Approx(4.0));   // 1 + 3

    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "SetStringOK", flag));
    REQUIRE(flag == true);

    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "RemoveOK", flag));
    REQUIRE(flag == true);
    REQUIRE_FALSE(entity.HasComponent<Forge::AudioSourceComponent>());

    // Each mistyped call must report false rather than being discarded. Before
    // this, every one of these was a silent no-op that looked like it worked.
    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "SetFloatBad", flag));
    REQUIRE(flag == false);

    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "SetBoolBad", flag));
    REQUIRE(flag == false);

    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "SetStringBad", flag));
    REQUIRE(flag == false);

    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "SetVector3Bad", flag));
    REQUIRE(flag == false);

    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "RemoveMissing", flag));
    REQUIRE(flag == false);

    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "RemoveUnknown", flag));
    REQUIRE(flag == false);

    // A failed write must not have corrupted anything on the way through: the
    // final Friction is what the legacy call set, not either of the failures.
    REQUIRE(collider.Friction == Approx(0.25f));
    REQUIRE(entity.GetComponent<Forge::TransformComponent>().Translation.x == Approx(4.0f));

    // Scripts written before the bindings reported failure ignore the answer;
    // they must keep working unchanged.
    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "LegacyStillRuns", flag));
    REQUIRE(flag == true);

    Forge::ScriptEngine::OnRuntimeStop();
}

TEST_CASE("Script state inspection covers booleans and writes back", "[scripting][inspect]")
{
    Forge::Log::Init();
    Forge::Scene scene;

    const char* kScriptDir = FORGE_SOURCE_DIR "/resources/scripts/";
    Forge::ScriptEngine::SetScriptDirectory(kScriptDir);

    auto entity = scene.CreateEntity("Inspector");

    // The script reads and writes these, and reports failure for anything
    // missing - so the entity needs them or every assertion below is about an
    // entity that never had a BoxCollider in the first place.
    auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
    rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    entity.AddComponent<Forge::BoxColliderComponent>().Size = glm::vec3(1.0f);

    auto& script = entity.AddComponent<Forge::ScriptComponent>();
    script.ClassName = "SetterReporting";

    Forge::ScriptEngine::OnRuntimeStart(&scene);

    bool flag = true;
    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "SetFloatOK", flag));
    REQUIRE(flag == true);

    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "SetFloatBad", flag));
    REQUIRE(flag == false);

    // A field that was never set is not a boolean, and must not read back as
    // false - that would be indistinguishable from a real false, which is the
    // mistake this API exists to rule out.
    REQUIRE_FALSE(Forge::ScriptEngine::GetScriptBool(entity, "NoSuchField", flag));

    // A numeric field is not a boolean either, so the same reasoning holds.
    double numeric = 0.0;
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(entity, "FrictionAfterGoodSet", numeric));
    REQUIRE_FALSE(Forge::ScriptEngine::GetScriptBool(entity, "FrictionAfterGoodSet", flag));

    // Writing back and reading again.
    REQUIRE(Forge::ScriptEngine::SetScriptBool(entity, "Injected", true));
    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "Injected", flag));
    REQUIRE(flag == true);

    REQUIRE(Forge::ScriptEngine::SetScriptBool(entity, "Injected", false));
    REQUIRE(Forge::ScriptEngine::GetScriptBool(entity, "Injected", flag));
    REQUIRE(flag == false);

    std::string text;
    REQUIRE(Forge::ScriptEngine::SetScriptString(entity, "Note", "hello"));
    REQUIRE(Forge::ScriptEngine::GetScriptString(entity, "Note", text));
    REQUIRE(text == "hello");

    double number = 0.0;
    REQUIRE(Forge::ScriptEngine::SetScriptNumber(entity, "Count", 42.0));
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(entity, "Count", number));
    REQUIRE(number == Approx(42.0));

    // A field set as a string must not read back as a number.
    REQUIRE_FALSE(Forge::ScriptEngine::GetScriptNumber(entity, "Note", number));

    // An entity with no script at all must report failure rather than reading
    // some other entity's state.
    auto bare = scene.CreateEntity("Bare");
    REQUIRE_FALSE(Forge::ScriptEngine::GetScriptBool(bare, "SetFloatOK", flag));
    REQUIRE_FALSE(Forge::ScriptEngine::SetScriptBool(bare, "Injected", true));

    Forge::ScriptEngine::OnRuntimeStop();
}

// ---------------------------------------------------------------------------
// GetDeltaTime
// ---------------------------------------------------------------------------

TEST_CASE("GetDeltaTime is bound and reports the frame's delta", "[scripting][deltatime]")
{
    Forge::Log::Init();
    Forge::Scene scene;

    const char* kScriptDir = FORGE_SOURCE_DIR "/resources/scripts/";
    Forge::ScriptEngine::SetScriptDirectory(kScriptDir);

    auto entity = scene.CreateEntity("Timer");
    auto& script = entity.AddComponent<Forge::ScriptComponent>();
    script.ClassName = "DeltaProbe";

    Forge::ScriptEngine::OnRuntimeStart(&scene);

    // Before any frame has run there is no delta to report. 0 is the honest
    // answer rather than a fallback value.
    double value = -1.0;
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(entity, "DeltaBeforeUpdate", value));
    REQUIRE(value == Approx(0.0));
    REQUIRE(Forge::ScriptEngine::GetLastDeltaTime() == Approx(0.0));

    scene.OnUpdateRuntime(Forge::Timestep(0.02f));

    // Inside OnUpdate it must be this frame's delta, not the previous one.
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(entity, "DeltaInUpdate", value));
    REQUIRE(value == Approx(0.02));

    // ...and it must agree with the dt the engine passed to OnUpdate.
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(entity, "DeltaArgument", value));
    REQUIRE(value == Approx(0.02));

    // Naming the entity explicitly gives the same answer.
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(entity, "DeltaFromSelf", value));
    REQUIRE(value == Approx(0.02));

    // With no argument, from inside the tick, it is also the current frame.
    REQUIRE(Forge::ScriptEngine::GetScriptNumber(entity, "DeltaNoArg", value));
    REQUIRE(value == Approx(0.02));

    // The C++ accessor agrees, so tooling sees the same number Lua does.
    REQUIRE(Forge::ScriptEngine::GetEntityDeltaTime(entity) == Approx(0.02));
    REQUIRE(Forge::ScriptEngine::GetLastDeltaTime() == Approx(0.02));

    Forge::ScriptEngine::OnRuntimeStop();
}

TEST_CASE("GetDeltaTime follows each frame rather than a fixed value", "[scripting][deltatime]")
{
    Forge::Log::Init();
    Forge::Scene scene;

    const char* kScriptDir = FORGE_SOURCE_DIR "/resources/scripts/";
    Forge::ScriptEngine::SetScriptDirectory(kScriptDir);

    auto entity = scene.CreateEntity("Timer");
    entity.AddComponent<Forge::ScriptComponent>().ClassName = "DeltaProbe";

    Forge::ScriptEngine::OnRuntimeStart(&scene);

    // A varying dt must show up. A binding that returned a constant, or lagged
    // a frame behind, would pass the single-frame test above.
    scene.OnUpdateRuntime(Forge::Timestep(0.02f));
    REQUIRE(Forge::ScriptEngine::GetEntityDeltaTime(entity) == Approx(0.02));

    scene.OnUpdateRuntime(Forge::Timestep(0.05f));
    REQUIRE(Forge::ScriptEngine::GetEntityDeltaTime(entity) == Approx(0.05));

    scene.OnUpdateRuntime(Forge::Timestep(0.01f));
    REQUIRE(Forge::ScriptEngine::GetEntityDeltaTime(entity) == Approx(0.01));

    // An entity that was never updated has no delta of its own, and must say
    // so rather than borrowing one that belongs to another entity.
    auto idle = scene.CreateEntity("Idle");
    REQUIRE(Forge::ScriptEngine::GetEntityDeltaTime(idle) == Approx(0.0));

    // Nor does a null entity.
    REQUIRE(Forge::ScriptEngine::GetEntityDeltaTime(Forge::Entity()) == Approx(0.0));

    Forge::ScriptEngine::OnRuntimeStop();
}
