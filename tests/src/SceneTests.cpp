#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"

#include <yaml-cpp/yaml.h>
#include <fstream>

TEST_CASE("Scene creates entities", "[scene][entity]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("TestEntity");
    REQUIRE(entity);
    REQUIRE(entity.GetName() == "TestEntity");
    REQUIRE(entity.HasComponent<Forge::IDComponent>());
    REQUIRE(entity.HasComponent<Forge::TransformComponent>());
    REQUIRE(entity.HasComponent<Forge::TagComponent>());
}

TEST_CASE("Scene creates entity with name", "[scene][entity]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("MyEntity");
    REQUIRE(entity.GetName() == "MyEntity");
}

TEST_CASE("Scene creates entity with UUID", "[scene][entity]")
{
    Forge::Scene scene;
    Forge::UUID testUUID(12345);

    auto entity = scene.CreateEntityWithUUID(testUUID, "NamedEntity");
    REQUIRE(entity.GetUUID() == testUUID);
    REQUIRE(entity.GetName() == "NamedEntity");
}

TEST_CASE("Scene destroys entity", "[scene][entity]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("ToDestroy");
    REQUIRE(entity);

    scene.DestroyEntity(entity);
    REQUIRE(!entity);
}

TEST_CASE("Scene finds entity by name", "[scene][entity]")
{
    Forge::Scene scene;

    scene.CreateEntity("Player");
    scene.CreateEntity("Enemy");

    auto player = scene.FindEntityByName("Player");
    REQUIRE(player);
    REQUIRE(player.GetName() == "Player");

    auto nonexistent = scene.FindEntityByName("NonExistent");
    REQUIRE(!nonexistent);
}

TEST_CASE("Scene finds entity by UUID", "[scene][entity]")
{
    Forge::Scene scene;
    Forge::UUID testUUID(99999);

    auto entity = scene.CreateEntityWithUUID(testUUID, "UUIDEntity");
    REQUIRE(scene.GetEntityByUUID(testUUID) == entity);
}

TEST_CASE("Scene duplicates entity", "[scene][entity]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("Original");
    auto& tc = entity.GetComponent<Forge::TransformComponent>();
    tc.Translation = { 1.0f, 2.0f, 3.0f };

    auto duplicate = scene.DuplicateEntity(entity);
    REQUIRE(duplicate);
    REQUIRE(duplicate.GetName() == "Original");
}

TEST_CASE("Scene component access", "[scene][component]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("Test");
    auto& transform = entity.GetComponent<Forge::TransformComponent>();

    REQUIRE(transform.Translation == glm::vec3(0.0f));
    REQUIRE(transform.Scale == glm::vec3(1.0f));
}

TEST_CASE("Scene transform component", "[scene][component]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("Test");
    auto& tc = entity.GetComponent<Forge::TransformComponent>();
    tc.Translation = { 1.0f, 2.0f, 3.0f };
    tc.Rotation = { 0.0f, 0.0f, 0.0f };
    tc.Scale = { 2.0f, 2.0f, 2.0f };

    REQUIRE(tc.Translation == glm::vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(tc.Scale == glm::vec3(2.0f, 2.0f, 2.0f));

    auto mat = tc.GetTransform();
    REQUIRE(mat[3][0] == Approx(1.0f));
    REQUIRE(mat[3][1] == Approx(2.0f));
    REQUIRE(mat[3][2] == Approx(3.0f));
}

TEST_CASE("Scene camera component", "[scene][component]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("Camera");
    auto& cc = entity.AddComponent<Forge::CameraComponent>();
    cc.Primary = true;

    REQUIRE(cc.Primary == true);
}

TEST_CASE("Scene rigidbody component", "[scene][component]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("Physics");
    auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();
    rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    rb.FixedRotation = false;

    REQUIRE(rb.Type == Forge::RigidbodyComponent::BodyType::Dynamic);
    REQUIRE(rb.FixedRotation == false);
}

TEST_CASE("Scene box collider component", "[scene][component]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("Collider");
    auto& bc = entity.AddComponent<Forge::BoxColliderComponent>();
    bc.Offset = { 0.0f, 0.5f, 0.0f };
    bc.Size = { 1.0f, 1.0f, 1.0f };
    bc.Density = 2.0f;
    bc.Friction = 0.8f;
    bc.Restitution = 0.3f;

    REQUIRE(bc.Offset == glm::vec3(0.0f, 0.5f, 0.0f));
    REQUIRE(bc.Size == glm::vec3(1.0f, 1.0f, 1.0f));
    REQUIRE(bc.Density == Approx(2.0f));
    REQUIRE(bc.Friction == Approx(0.8f));
    REQUIRE(bc.Restitution == Approx(0.3f));
}

TEST_CASE("Scene sphere collider component", "[scene][component]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("Sphere");
    auto& sc = entity.AddComponent<Forge::SphereColliderComponent>();
    sc.Offset = { 0.0f, 0.5f, 0.0f };
    sc.Radius = 0.5f;

    REQUIRE(sc.Offset == glm::vec3(0.0f, 0.5f, 0.0f));
    REQUIRE(sc.Radius == Approx(0.5f));
}

TEST_CASE("Scene PBR material component", "[scene][component]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("Material");
    auto& pbr = entity.AddComponent<Forge::PBRMaterialComponent>();
    pbr.Albedo = { 1.0f, 0.5f, 0.0f };
    pbr.Metallic = 0.8f;
    pbr.Roughness = 0.3f;
    pbr.Emissive = { 0.0f, 0.0f, 1.0f };
    pbr.EmissiveIntensity = 2.0f;

    REQUIRE(pbr.Albedo == glm::vec3(1.0f, 0.5f, 0.0f));
    REQUIRE(pbr.Metallic == Approx(0.8f));
    REQUIRE(pbr.Roughness == Approx(0.3f));
    REQUIRE(pbr.Emissive == glm::vec3(0.0f, 0.0f, 1.0f));
    REQUIRE(pbr.EmissiveIntensity == Approx(2.0f));
}

TEST_CASE("Scene light component", "[scene][component]")
{
    Forge::Scene scene;

    auto entity = scene.CreateEntity("Light");
    auto& light = entity.AddComponent<Forge::LightComponent>();
    light.Type = Forge::LightComponent::Type::Directional;
    light.Color = { 1.0f, 0.8f, 0.6f };
    light.Intensity = 2.0f;
    light.Direction = { 0.0f, -1.0f, 0.0f };

    REQUIRE(light.Type == Forge::LightComponent::Type::Directional);
    REQUIRE(light.Color == glm::vec3(1.0f, 0.8f, 0.6f));
    REQUIRE(light.Intensity == Approx(2.0f));
}

TEST_CASE("Scene entity count", "[scene][count]")
{
    Forge::Scene scene;

    scene.CreateEntity("A");
    scene.CreateEntity("B");
    scene.CreateEntity("C");

    auto view = scene.GetAllEntitiesWith<Forge::TagComponent>();
    int count = 0;
    for (auto e : view) (void)e, count++;

    REQUIRE(count == 3);
}

TEST_CASE("Scene destroys entity removes from view", "[scene][destroy]")
{
    Forge::Scene scene;

    auto entity1 = scene.CreateEntity("A");
    scene.CreateEntity("B");

    auto view = scene.GetAllEntitiesWith<Forge::TagComponent>();
    int count = 0;
    for (auto e : view) (void)e, count++;
    REQUIRE(count == 2);

    scene.DestroyEntity(entity1);

    view = scene.GetAllEntitiesWith<Forge::TagComponent>();
    count = 0;
    for (auto e : view) (void)e, count++;
    REQUIRE(count == 1);
}

TEST_CASE("Scene serialization round-trip", "[scene][serializer]")
{
    Forge::Scene scene;

    // Create entities
    auto entity1 = scene.CreateEntity("Player");
    auto& tc1 = entity1.GetComponent<Forge::TransformComponent>();
    tc1.Translation = { 1.0f, 2.0f, 3.0f };
    tc1.Rotation = { 0.0f, 45.0f, 0.0f };
    tc1.Scale = { 2.0f, 2.0f, 2.0f };

    auto entity2 = scene.CreateEntity("Camera");
    auto& cc = entity2.AddComponent<Forge::CameraComponent>();
    cc.Primary = true;

    auto entity3 = scene.CreateEntity("Physics");
    auto& rb = entity3.AddComponent<Forge::RigidbodyComponent>();
    rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    rb.FixedRotation = true;

    // Serialize
    std::string filepath = "test_scene.forge";
    auto tempScene = Forge::CreateRef<Forge::Scene>();

    // Copy Player entity
    auto id1 = entity1.GetComponent<Forge::IDComponent>().ID;
    auto& tComp1 = entity1.GetComponent<Forge::TransformComponent>();
    auto& tag1 = entity1.GetComponent<Forge::TagComponent>();
    auto newEntity1 = tempScene->CreateEntityWithUUID(id1, tag1.Tag);
    auto& newTC1 = newEntity1.GetComponent<Forge::TransformComponent>();
    newTC1.Translation = tComp1.Translation;
    newTC1.Rotation = tComp1.Rotation;
    newTC1.Scale = tComp1.Scale;

    // Copy Camera entity
    auto id2 = entity2.GetComponent<Forge::IDComponent>().ID;
    auto& cComp = entity2.GetComponent<Forge::CameraComponent>();
    auto newEntity2 = tempScene->CreateEntityWithUUID(id2, "Camera");
    auto& newCC2 = newEntity2.AddComponent<Forge::CameraComponent>();
    newCC2.Primary = cComp.Primary;

    // Copy Physics entity
    auto id3 = entity3.GetComponent<Forge::IDComponent>().ID;
    auto& rComp = entity3.GetComponent<Forge::RigidbodyComponent>();
    auto newEntity3 = tempScene->CreateEntityWithUUID(id3, "Physics");
    auto& newRB3 = newEntity3.AddComponent<Forge::RigidbodyComponent>();
    newRB3.Type = rComp.Type;
    newRB3.FixedRotation = rComp.FixedRotation;

    Forge::SceneSerializer serializer(tempScene);
    REQUIRE(serializer.Serialize(filepath));

    // Verify the serialized file exists and is valid
    std::ifstream verifyFile(filepath);
    REQUIRE(verifyFile.is_open());

    // Deserialize into new scene (round-trip verification)
    auto newSceneRef = Forge::CreateRef<Forge::Scene>();
    Forge::SceneSerializer newSerializer(newSceneRef);
    bool result = newSerializer.Deserialize(filepath);
    REQUIRE(result == true);

    // Verify deserialized entities
    REQUIRE(newSceneRef->GetEntityByUUID(id1));
    REQUIRE(newSceneRef->GetEntityByUUID(id2));
    REQUIRE(newSceneRef->GetEntityByUUID(id3));

    // Verify component data was preserved
    auto deserializedEntity1 = newSceneRef->GetEntityByUUID(id1);
    REQUIRE(deserializedEntity1.GetName() == "Player");
    auto& deserializedTC1 = deserializedEntity1.GetComponent<Forge::TransformComponent>();
    REQUIRE(deserializedTC1.Translation == glm::vec3(1.0f, 2.0f, 3.0f));
    REQUIRE(deserializedTC1.Scale == glm::vec3(2.0f, 2.0f, 2.0f));
}
