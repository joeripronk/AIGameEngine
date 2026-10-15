#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Core/Log.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Physics/Physics.h"
#include "Forge/Scripting/ScriptEngine.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// These tests load the canonical comprehensive scene (resources/scenes/
// EngineTest.forge) that exercises every engine component and the scripting
// API end-to-end.

TEST_CASE("Comprehensive test scene loads every component", "[scene][comprehensive]")
{
    Forge::Log::Init();

    auto scene = Forge::CreateRef<Forge::Scene>();
    Forge::SceneSerializer serializer(scene);
    REQUIRE(serializer.Deserialize(std::string(FORGE_SOURCE_DIR) + "/resources/scenes/EngineTest.forge"));

    // Every expected entity is present.
    auto camera = scene->FindEntityByName("MainCamera");
    auto sun = scene->FindEntityByName("SunLight");
    auto lamp = scene->FindEntityByName("PointLight");
    auto ground = scene->FindEntityByName("Ground");
    auto box = scene->FindEntityByName("FallingBox");
    auto sphere = scene->FindEntityByName("RollingSphere");
    auto mover = scene->FindEntityByName("ScriptedMover");
    auto rotor = scene->FindEntityByName("ScriptedRotator");

    REQUIRE(camera);
    REQUIRE(sun);
    REQUIRE(lamp);
    REQUIRE(ground);
    REQUIRE(box);
    REQUIRE(sphere);
    REQUIRE(mover);
    REQUIRE(rotor);

    // Component coverage across the whole scene.
    REQUIRE(camera.HasComponent<Forge::CameraComponent>());
    REQUIRE(camera.GetComponent<Forge::CameraComponent>().Primary);

    REQUIRE(sun.HasComponent<Forge::LightComponent>());
    REQUIRE(sun.GetComponent<Forge::LightComponent>().Type == Forge::LightComponent::Type::Directional);

    REQUIRE(lamp.HasComponent<Forge::LightComponent>());
    REQUIRE(lamp.GetComponent<Forge::LightComponent>().Type == Forge::LightComponent::Type::Point);
    REQUIRE(lamp.GetComponent<Forge::LightComponent>().Color == glm::vec3(1.0f, 0.6f, 0.2f));

    // Static ground: rigidbody + box collider.
    REQUIRE(ground.HasComponent<Forge::RigidbodyComponent>());
    REQUIRE(ground.GetComponent<Forge::RigidbodyComponent>().Type == Forge::RigidbodyComponent::BodyType::Static);
    REQUIRE(ground.GetComponent<Forge::BoxColliderComponent>().Size == glm::vec3(20.0f, 1.0f, 20.0f));

    // Dynamic box: rigidbody + box collider + PBR material + mesh.
    REQUIRE(box.HasComponent<Forge::RigidbodyComponent>());
    REQUIRE(box.GetComponent<Forge::RigidbodyComponent>().Type == Forge::RigidbodyComponent::BodyType::Dynamic);
    REQUIRE(box.HasComponent<Forge::BoxColliderComponent>());
    REQUIRE(box.GetComponent<Forge::BoxColliderComponent>().Restitution == Approx(0.4f));
    REQUIRE(box.HasComponent<Forge::PBRMaterialComponent>());
    REQUIRE(box.GetComponent<Forge::PBRMaterialComponent>().Metallic == Approx(0.1f));
    REQUIRE(box.HasComponent<Forge::MeshComponent>());
    REQUIRE(box.GetComponent<Forge::MeshComponent>().ModelPath == "resources/models/test_triangle.obj");

    // Sphere: dynamic rigidbody + sphere collider + PBR material.
    REQUIRE(sphere.HasComponent<Forge::RigidbodyComponent>());
    REQUIRE(sphere.HasComponent<Forge::SphereColliderComponent>());
    REQUIRE(sphere.GetComponent<Forge::SphereColliderComponent>().Radius == Approx(0.5f));
    REQUIRE(sphere.GetComponent<Forge::PBRMaterialComponent>().Metallic == Approx(0.9f));

    // Scripted entities carry a ScriptComponent naming a real script.
    REQUIRE(mover.GetComponent<Forge::ScriptComponent>().ClassName == "Mover");
    REQUIRE(rotor.GetComponent<Forge::ScriptComponent>().ClassName == "Rotator");
}

TEST_CASE("Comprehensive test scene drives physics and scripting", "[scene][comprehensive][runtime]")
{
    Forge::Log::Init();
    Forge::ScriptEngine::Init();
    Forge::Physics::Init();

    auto scene = Forge::CreateRef<Forge::Scene>();
    Forge::SceneSerializer serializer(scene);
    REQUIRE(serializer.Deserialize(std::string(FORGE_SOURCE_DIR) + "/resources/scenes/EngineTest.forge"));

    auto box = scene->FindEntityByName("FallingBox");
    auto mover = scene->FindEntityByName("ScriptedMover");
    auto rotor = scene->FindEntityByName("ScriptedRotator");
    REQUIRE(box);
    REQUIRE(mover);
    REQUIRE(rotor);

    float boxStartY = box.GetComponent<Forge::TransformComponent>().Translation.y;

    // Point the engine at the committed scripts directory.
    Forge::ScriptEngine::SetScriptDirectory(std::string(FORGE_SOURCE_DIR) + "/resources/scripts/");

    // Enter play mode: loads scripts, calls OnCreate, and creates physics bodies.
    scene->OnRuntimeStart();

    // OnCreate for Mover resets the position to the origin.
    REQUIRE(mover.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(0.0f, 0.0f, 0.0f));

    // Drive a number of fixed frames.
    const float dt = 1.0f / 60.0f;
    for (int i = 0; i < 10; i++)
        scene->OnUpdateRuntime(Forge::Timestep(dt));

    // Physics moved the falling box down from its start height.
    REQUIRE(box.GetComponent<Forge::TransformComponent>().Translation.y < boxStartY);

    // Mover's OnUpdate advanced it along +X.
    REQUIRE(mover.GetComponent<Forge::TransformComponent>().Translation.x > 0.0f);

    // Rotator's OnUpdate spun it around Y (90 deg/sec * 10 frames).
    REQUIRE(rotor.GetComponent<Forge::TransformComponent>().Rotation.y > 0.0f);

    scene->OnRuntimeStop();

    Forge::Physics::Shutdown();
    Forge::ScriptEngine::Shutdown();
}
// ---------------------------------------------------------------------------
// The scene is comprehensive, and everything it names actually exists
// ---------------------------------------------------------------------------

TEST_CASE("Every asset the test scene names exists", "[scene][comprehensive]")
{
    // The scene referenced resources/primitives/cube.obj, which is not in the
    // repository, so the mesh it pointed at silently resolved to nothing. The
    // test that checked the path only checked the *string*, so it passed while
    // pointing at a file that does not exist.
    //
    // Anything a scene names is a promise that the file is there; a reference
    // that does not resolve is indistinguishable from a typo at authoring time.

    const std::string scenePath = std::string(FORGE_SOURCE_DIR) + "/resources/scenes/EngineTest.forge";
    std::ifstream in(scenePath);
    REQUIRE(in.is_open());

    const std::string contents((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());

    // The asset-bearing fields the serializer understands.
    const char* keys[] = { "SourcePath:", "ModelPath:", "Path:" };

    std::vector<std::string> referenced;
    size_t at = 0;

    while (at < contents.size())
    {
        for (const char* key : keys)
        {
            const std::string needle = key;
            if (contents.compare(at, needle.size(), needle) != 0)
                continue;

            at += needle.size();
            while (at < contents.size() && (contents[at] == ' ' || contents[at] == '\t'))
                at++;

            const size_t end = contents.find('\n', at);
            if (end == std::string::npos)
                break;

            referenced.push_back(contents.substr(at, end - at));
            at = end;
        }

        at++;
    }

    // The scene is only meaningful if it names assets at all.
    REQUIRE_FALSE(referenced.empty());

    for (const auto& path : referenced)
    {
        INFO("scene references '" << path << "'");
        REQUIRE_FALSE(path.empty());
        REQUIRE(std::filesystem::exists(std::string(FORGE_SOURCE_DIR) + "/" + path));
    }
}

TEST_CASE("The test scene covers every serialisable component", "[scene][comprehensive]")
{
    // GAMEENGINE.md asks for a scene exercising every feature. It drifted: audio
    // and environment maps were missing entirely, and a component added later
    // would have joined them silently. This makes that drift a build failure.

    auto scene = Forge::CreateRef<Forge::Scene>();
    Forge::SceneSerializer serializer(scene);
    REQUIRE(serializer.Deserialize(std::string(FORGE_SOURCE_DIR) + "/resources/scenes/EngineTest.forge"));

    // Excluded, with the reason:
    //
    //  - IDComponent, TagComponent and TransformComponent are added to every
    //    entity automatically, so they need no explicit coverage.
    //  - NativeScriptComponent holds function pointers bound via Bind<T>(),
    //    which a scene file cannot express. It is runtime-only, and covered by
    //    NativeScriptTests instead.

    // Each component must appear on *some* entity in the scene. Requiring one
    // entity to carry all of them would be wrong - they belong on different
    // objects - but their absence anywhere is the drift this catches.
    // A value argument rather than a type parameter, so the list below reads as
    // "does any entity carry a CameraComponent" at a glance.
    const auto anyHas = [&scene](auto tag)
    {
        using T = decltype(tag);

        for (Forge::Entity entity : scene->GetEntitiesInOrder())
        {
            if (entity.HasComponent<T>())
                return true;
        }
        return false;
    };

    REQUIRE(anyHas(Forge::CameraComponent{}));
    REQUIRE(anyHas(Forge::LightComponent{}));
    REQUIRE(anyHas(Forge::RigidbodyComponent{}));
    REQUIRE(anyHas(Forge::BoxColliderComponent{}));
    REQUIRE(anyHas(Forge::SphereColliderComponent{}));
    REQUIRE(anyHas(Forge::MeshComponent{}));
    REQUIRE(anyHas(Forge::PBRMaterialComponent{}));
    REQUIRE(anyHas(Forge::ScriptComponent{}));
    REQUIRE(anyHas(Forge::AudioSourceComponent{}));
    REQUIRE(anyHas(Forge::EnvironmentMapComponent{}));
}
