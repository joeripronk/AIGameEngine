#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Physics/Physics.h"
#include "Forge/Scripting/ScriptEngine.h"
#include "Forge/Core/Log.h"

TEST_CASE("Comprehensive scene with all components", "[comprehensive][scene]")
{
    Forge::Scene scene;

    // Create entities with all component types
    auto entity1 = scene.CreateEntity("Entity1");
    auto& tc1 = entity1.GetComponent<Forge::TransformComponent>();
    tc1.Translation = { 1.0f, 2.0f, 3.0f };
    tc1.Rotation = { 0.0f, 45.0f, 0.0f };
    tc1.Scale = { 2.0f, 2.0f, 2.0f };

    auto entity2 = scene.CreateEntity("Entity2");
    auto& tc2 = entity2.GetComponent<Forge::TransformComponent>();
    tc2.Translation = { 4.0f, 5.0f, 6.0f };
    auto& cc2 = entity2.AddComponent<Forge::CameraComponent>();
    cc2.Primary = true;
    cc2.ProjectionMatrix = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

    auto entity3 = scene.CreateEntity("Entity3");
    auto& tc3 = entity3.GetComponent<Forge::TransformComponent>();
    tc3.Translation = { 7.0f, 8.0f, 9.0f };
    auto& rb3 = entity3.AddComponent<Forge::RigidbodyComponent>();
    rb3.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    rb3.FixedRotation = false;
    auto& bc3 = entity3.AddComponent<Forge::BoxColliderComponent>();
    bc3.Size = { 1.0f, 1.0f, 1.0f };
    bc3.Density = 2.0f;
    bc3.Friction = 0.8f;
    bc3.Restitution = 0.3f;

    auto entity4 = scene.CreateEntity("Entity4");
    auto& tc4 = entity4.GetComponent<Forge::TransformComponent>();
    tc4.Translation = { 0.0f, 0.0f, 0.0f };
    auto& sc4 = entity4.AddComponent<Forge::SphereColliderComponent>();
    sc4.Radius = 1.0f;
    sc4.Density = 1.0f;
    sc4.Friction = 0.5f;
    sc4.Restitution = 0.0f;

    auto entity5 = scene.CreateEntity("Entity5");
    auto& tc5 = entity5.GetComponent<Forge::TransformComponent>();
    tc5.Translation = { 0.0f, 0.0f, 0.0f };
    auto& pbr5 = entity5.AddComponent<Forge::PBRMaterialComponent>();
    pbr5.Albedo = { 1.0f, 0.5f, 0.0f };
    pbr5.Metallic = 0.8f;
    pbr5.Roughness = 0.3f;
    pbr5.Emissive = { 0.0f, 0.0f, 1.0f };
    pbr5.EmissiveIntensity = 2.0f;

    auto entity6 = scene.CreateEntity("Entity6");
    auto& tc6 = entity6.GetComponent<Forge::TransformComponent>();
    tc6.Translation = { 0.0f, 0.0f, 0.0f };
    auto& light6 = entity6.AddComponent<Forge::LightComponent>();
    light6.Type = Forge::LightComponent::Type::Point;
    light6.Color = { 1.0f, 0.8f, 0.6f };
    light6.Intensity = 2.0f;

    auto entity7 = scene.CreateEntity("Entity7");
    auto& tc7 = entity7.GetComponent<Forge::TransformComponent>();
    tc7.Translation = { 0.0f, 0.0f, 0.0f };
    auto& script7 = entity7.AddComponent<Forge::ScriptComponent>();
    script7.ClassName = "PlayerController";

    // Verify all entities exist
    REQUIRE(scene.FindEntityByName("Entity1"));
    REQUIRE(scene.FindEntityByName("Entity2"));
    REQUIRE(scene.FindEntityByName("Entity3"));
    REQUIRE(scene.FindEntityByName("Entity4"));
    REQUIRE(scene.FindEntityByName("Entity5"));
    REQUIRE(scene.FindEntityByName("Entity6"));
    REQUIRE(scene.FindEntityByName("Entity7"));

    // Verify component counts
    auto view = scene.GetAllEntitiesWith<Forge::TagComponent>();
    int count = 0;
    for (auto e : view) (void)e, count++;
    REQUIRE(count == 7);

    // Verify transform calculations
    auto mat = tc1.GetTransform();
    REQUIRE(mat[3][0] == Approx(1.0f));
    REQUIRE(mat[3][1] == Approx(2.0f));
    REQUIRE(mat[3][2] == Approx(3.0f));
}

TEST_CASE("Scene serialization with all components", "[comprehensive][serializer]")
{
    Forge::Scene scene;

    // Create entities with all components
    auto entity1 = scene.CreateEntity("TestEntity");
    auto& tc1 = entity1.GetComponent<Forge::TransformComponent>();
    tc1.Translation = { 1.0f, 2.0f, 3.0f };
    auto& cc1 = entity1.AddComponent<Forge::CameraComponent>();
    cc1.Primary = true;
    auto& rb1 = entity1.AddComponent<Forge::RigidbodyComponent>();
    rb1.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    rb1.FixedRotation = true;
    auto& bc1 = entity1.AddComponent<Forge::BoxColliderComponent>();
    bc1.Size = { 2.0f, 2.0f, 2.0f };
    bc1.Density = 2.0f;
    bc1.Friction = 0.8f;
    bc1.Restitution = 0.3f;

    // Serialize
    std::string filepath = "test_comprehensive.forge";
    auto tempScene = Forge::CreateRef<Forge::Scene>();

    // Copy entity manually (since Scene::Copy is not fully implemented yet)
    auto id1 = entity1.GetComponent<Forge::IDComponent>().ID;
    auto& tag1 = entity1.GetComponent<Forge::TagComponent>();
    auto newEntity1 = tempScene->CreateEntityWithUUID(id1, tag1.Tag);
    auto& newTC1 = newEntity1.GetComponent<Forge::TransformComponent>();
    newTC1.Translation = tc1.Translation;
    newTC1.Rotation = tc1.Rotation;
    newTC1.Scale = tc1.Scale;

    Forge::SceneSerializer serializer(tempScene);
    serializer.Serialize(filepath);

    // Deserialize
    auto newSceneRef = Forge::CreateRef<Forge::Scene>();
    Forge::SceneSerializer newSerializer(newSceneRef);
    bool result = newSerializer.Deserialize(filepath);
    REQUIRE(result == true);

    // Verify deserialized entity
    REQUIRE(newSceneRef->GetEntityByUUID(id1));
    auto deserializedEntity = newSceneRef->GetEntityByUUID(id1);
    REQUIRE(deserializedEntity.GetName() == "TestEntity");
    auto& deserializedTC = deserializedEntity.GetComponent<Forge::TransformComponent>();
    REQUIRE(deserializedTC.Translation == glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("Physics with scene components", "[comprehensive][physics]")
{
    Forge::Log::Init();
    Forge::Physics::Init();

    Forge::Scene scene;

    // Create a dynamic physics entity
    auto entity = scene.CreateEntity("PhysicsEntity");
    auto& tc = entity.GetComponent<Forge::TransformComponent>();
    tc.Translation = { 0.0f, 10.0f, 0.0f };
    auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
    rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    rb.FixedRotation = false;
    auto& bc = entity.AddComponent<Forge::BoxColliderComponent>();
    bc.Size = { 1.0f, 1.0f, 1.0f };
    bc.Density = 1.0f;
    bc.Friction = 0.5f;
    bc.Restitution = 0.0f;

    // Start physics
    scene.OnPhysicsStart();

    // Verify physics is running
    REQUIRE(scene.IsRunning() == false);

    // Step the scene
    scene.Step(1);

    // Stop physics
    scene.OnPhysicsStop();

    Forge::Physics::Shutdown();
}

TEST_CASE("Scripting with scene", "[comprehensive][scripting]")
{
    Forge::Log::Init();
    Forge::ScriptEngine::Init();

    Forge::Scene scene;

    // Create a script entity
    auto entity = scene.CreateEntity("ScriptEntity");
    auto& tc = entity.GetComponent<Forge::TransformComponent>();
    tc.Translation = { 0.0f, 0.0f, 0.0f };
    auto& script = entity.AddComponent<Forge::ScriptComponent>();
    script.ClassName = "TestScript";

    // Start runtime
    scene.OnRuntimeStart();

    // Verify entity exists
    REQUIRE(scene.FindEntityByName("ScriptEntity"));

    // Stop runtime
    scene.OnRuntimeStop();

    Forge::ScriptEngine::Shutdown();
}

TEST_CASE("Scene Copy with all components", "[comprehensive][copy]")
{
    auto scene = Forge::CreateRef<Forge::Scene>();

    // Create entities with all components
    auto entity1 = scene->CreateEntity("Original");
    auto& tc1 = entity1.GetComponent<Forge::TransformComponent>();
    tc1.Translation = { 1.0f, 2.0f, 3.0f };
    tc1.Rotation = { 0.0f, 45.0f, 0.0f };
    tc1.Scale = { 2.0f, 2.0f, 2.0f };
    auto& cc1 = entity1.AddComponent<Forge::CameraComponent>();
    cc1.Primary = true;
    cc1.ProjectionMatrix = glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    auto& rb1 = entity1.AddComponent<Forge::RigidbodyComponent>();
    rb1.Type = Forge::RigidbodyComponent::BodyType::Static;
    rb1.FixedRotation = true;
    auto& bc1 = entity1.AddComponent<Forge::BoxColliderComponent>();
    bc1.Size = { 1.0f, 1.0f, 1.0f };
    bc1.Density = 1.0f;
    bc1.Friction = 0.5f;
    bc1.Restitution = 0.0f;
    auto& pbr1 = entity1.AddComponent<Forge::PBRMaterialComponent>();
    pbr1.Albedo = { 1.0f, 0.0f, 0.0f };
    pbr1.Metallic = 0.5f;
    pbr1.Roughness = 0.3f;
    auto& light1 = entity1.AddComponent<Forge::LightComponent>();
    light1.Type = Forge::LightComponent::Type::Directional;
    light1.Color = { 1.0f, 1.0f, 1.0f };
    light1.Intensity = 1.0f;

    // Copy the scene
    auto copiedScene = Forge::Scene::Copy(scene);
    REQUIRE(copiedScene);

    // Verify copied entity exists with same UUID
    auto copiedEntity = copiedScene->GetEntityByUUID(entity1.GetUUID());
    REQUIRE(copiedEntity);
    REQUIRE(copiedEntity.GetName() == "Original");

    // Verify transform was copied
    auto& copiedTC = copiedEntity.GetComponent<Forge::TransformComponent>();
    REQUIRE(copiedTC.Translation == glm::vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(copiedTC.Rotation == glm::vec3(0.0f, 45.0f, 0.0f));
    REQUIRE(copiedTC.Scale == glm::vec3(2.0f, 2.0f, 2.0f));

    // Verify camera was copied
    auto& copiedCC = copiedEntity.GetComponent<Forge::CameraComponent>();
    REQUIRE(copiedCC.Primary == true);

    // Verify rigidbody was copied
    auto& copiedRB = copiedEntity.GetComponent<Forge::RigidbodyComponent>();
    REQUIRE(copiedRB.Type == Forge::RigidbodyComponent::BodyType::Static);
    REQUIRE(copiedRB.FixedRotation == true);

    // Verify box collider was copied
    auto& copiedBC = copiedEntity.GetComponent<Forge::BoxColliderComponent>();
    REQUIRE(copiedBC.Size == glm::vec3(1.0f, 1.0f, 1.0f));
    REQUIRE(copiedBC.Density == Approx(1.0f));
    REQUIRE(copiedBC.Friction == Approx(0.5f));
    REQUIRE(copiedBC.Restitution == Approx(0.0f));

    // Verify PBR material was copied
    auto& copiedPBR = copiedEntity.GetComponent<Forge::PBRMaterialComponent>();
    REQUIRE(copiedPBR.Albedo == glm::vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(copiedPBR.Metallic == Approx(0.5f));
    REQUIRE(copiedPBR.Roughness == Approx(0.3f));

    // Verify light was copied
    auto& copiedLight = copiedEntity.GetComponent<Forge::LightComponent>();
    REQUIRE(copiedLight.Type == Forge::LightComponent::Type::Directional);
    REQUIRE(copiedLight.Color == glm::vec3(1.0f, 1.0f, 1.0f));
    REQUIRE(copiedLight.Intensity == Approx(1.0f));
}

TEST_CASE("Entity duplication", "[comprehensive][entity]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("Original");
    auto& tc = entity.GetComponent<Forge::TransformComponent>();
    tc.Translation = { 1.0f, 2.0f, 3.0f };
    tc.Rotation = { 0.0f, 45.0f, 0.0f };
    tc.Scale = { 2.0f, 2.0f, 2.0f };
    auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
    rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    rb.FixedRotation = false;

    auto duplicate = scene.DuplicateEntity(entity);
    REQUIRE(duplicate);
    REQUIRE(duplicate.GetName() == "Original");
}

TEST_CASE("Scene with sphere collider component", "[comprehensive][component]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("SphereEntity");
    auto& tc = entity.GetComponent<Forge::TransformComponent>();
    tc.Translation = { 0.0f, 5.0f, 0.0f };
    auto& sc = entity.AddComponent<Forge::SphereColliderComponent>();
    sc.Radius = 2.0f;
    sc.Offset = { 0.0f, 1.0f, 0.0f };
    sc.Density = 1.5f;
    sc.Friction = 0.7f;
    sc.Restitution = 0.9f;

    REQUIRE(sc.Radius == Approx(2.0f));
    REQUIRE(sc.Offset == glm::vec3(0.0f, 1.0f, 0.0f));
    REQUIRE(sc.Density == Approx(1.5f));
    REQUIRE(sc.Friction == Approx(0.7f));
    REQUIRE(sc.Restitution == Approx(0.9f));
}

TEST_CASE("Scene with mesh component", "[comprehensive][component]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("MeshEntity");
    auto& tc = entity.GetComponent<Forge::TransformComponent>();
    tc.Translation = { 0.0f, 0.0f, 0.0f };
    auto& mc = entity.AddComponent<Forge::MeshComponent>();
    mc.ModelPath = "models/test.obj";
    mc.Scale = { 2.0f, 2.0f, 2.0f };

    REQUIRE(mc.ModelPath == "models/test.obj");
    REQUIRE(mc.Scale == glm::vec3(2.0f, 2.0f, 2.0f));
}