#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Renderer/CpuMesh.h"

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

namespace {
    // An entity carrying every component the engine defines. Duplication and
    // scene copy are supposed to preserve all of them, and the only way that
    // stays true as components are added is for a test to notice when one is
    // left out.
    Forge::Entity MakeFullyLoadedEntity(Forge::Scene& scene, const std::string& name)
    {
        Forge::Entity entity = scene.CreateEntity(name);

        entity.GetComponent<Forge::TagComponent>().Tag = name + "Tag";
        entity.GetComponent<Forge::TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };

        entity.AddComponent<Forge::CameraComponent>().FieldOfView = 33.0f;
        entity.AddComponent<Forge::ScriptComponent>().ClassName = "SomeScript";
        entity.AddComponent<Forge::RigidbodyComponent>().FixedRotation = true;
        entity.AddComponent<Forge::BoxColliderComponent>().Size = { 2.0f, 2.0f, 2.0f };
        entity.AddComponent<Forge::SphereColliderComponent>().Radius = 3.0f;
        entity.AddComponent<Forge::LightComponent>().Intensity = 7.0f;
        entity.AddComponent<Forge::PBRMaterialComponent>().Metallic = 0.6f;
        entity.AddComponent<Forge::MeshComponent>().ModelPath = "models/thing.glb";
        entity.AddComponent<Forge::EnvironmentMapComponent>().Path = "environment/sunset.hdr";

        auto& audio = entity.AddComponent<Forge::AudioSourceComponent>();
        audio.SourcePath = "audio/thing.wav";
        audio.Volume = 0.25f;

        return entity;
    }

    void RequireEveryComponentCopied(Forge::Entity entity)
    {
        REQUIRE(entity.HasComponent<Forge::TagComponent>());
        REQUIRE(entity.HasComponent<Forge::TransformComponent>());
        REQUIRE(entity.HasComponent<Forge::CameraComponent>());
        REQUIRE(entity.HasComponent<Forge::ScriptComponent>());
        REQUIRE(entity.HasComponent<Forge::RigidbodyComponent>());
        REQUIRE(entity.HasComponent<Forge::BoxColliderComponent>());
        REQUIRE(entity.HasComponent<Forge::SphereColliderComponent>());
        REQUIRE(entity.HasComponent<Forge::LightComponent>());
        REQUIRE(entity.HasComponent<Forge::PBRMaterialComponent>());
        REQUIRE(entity.HasComponent<Forge::MeshComponent>());
        REQUIRE(entity.HasComponent<Forge::EnvironmentMapComponent>());
        REQUIRE(entity.HasComponent<Forge::AudioSourceComponent>());

        REQUIRE(entity.GetComponent<Forge::CameraComponent>().FieldOfView == Approx(33.0f));
        REQUIRE(entity.GetComponent<Forge::TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
        REQUIRE(entity.GetComponent<Forge::LightComponent>().Intensity == Approx(7.0f));
        REQUIRE(entity.GetComponent<Forge::PBRMaterialComponent>().Metallic == Approx(0.6f));
        REQUIRE(entity.GetComponent<Forge::MeshComponent>().ModelPath == "models/thing.glb");
        REQUIRE(entity.GetComponent<Forge::EnvironmentMapComponent>().Path == "environment/sunset.hdr");
        REQUIRE(entity.GetComponent<Forge::AudioSourceComponent>().SourcePath == "audio/thing.wav");
        REQUIRE(entity.GetComponent<Forge::AudioSourceComponent>().Volume == Approx(0.25f));
    }
}

TEST_CASE("Duplicating an entity copies every component", "[scene][entity][copy]")
{
    Forge::Scene scene;
    Forge::Entity original = MakeFullyLoadedEntity(scene, "Full");

    Forge::Entity copy = scene.DuplicateEntity(original);
    REQUIRE(copy);
    REQUIRE(copy != original);

    // A duplicate is a new entity: its own UUID, its own identity.
    REQUIRE(copy.GetUUID() != original.GetUUID());

    RequireEveryComponentCopied(copy);
}

TEST_CASE("Scene::Copy preserves every component", "[scene][copy]")
{
    Forge::Ref<Forge::Scene> scene = Forge::CreateRef<Forge::Scene>();
    Forge::Entity original = MakeFullyLoadedEntity(*scene, "Full");
    const Forge::UUID originalId = original.GetUUID();

    Forge::Ref<Forge::Scene> copy = scene->Copy(scene);
    REQUIRE(copy);

    // Found by UUID: the helper re-tags the entity, so its name is not "Full".
    Forge::Entity copied = copy->GetEntityByUUID(originalId);
    REQUIRE(copied);
    RequireEveryComponentCopied(copied);

    // A scene copy preserves UUIDs, so it can be saved and reopened as the same
    // scene rather than a different one.
    REQUIRE(copied.GetUUID() == originalId);
}

TEST_CASE("Scene::Copy reports a missing source", "[scene][copy]")
{
    Forge::Scene scene;
    REQUIRE(scene.Copy(Forge::Ref<Forge::Scene>()) == nullptr);
}

TEST_CASE("The primary camera is resolved once, in authoring order", "[scene][camera]")
{
    Forge::Scene scene;

    // No camera yet.
    REQUIRE(!scene.GetPrimaryCameraEntity());
    REQUIRE(scene.GetAllCameraEntities().empty());

    Forge::Entity first = scene.CreateEntity("First");
    first.AddComponent<Forge::CameraComponent>();
    Forge::Entity second = scene.CreateEntity("Second");
    second.AddComponent<Forge::CameraComponent>();

    // Both default to primary, and that is ambiguous by construction: the
    // answer is the first one in authoring order, not whichever the registry
    // happened to yield.
    REQUIRE(scene.GetPrimaryCameraEntity() == first);
    REQUIRE(scene.GetAllCameraEntities().size() == 2u);

    // Choosing the other one demotes the first, so "the primary camera" has an
    // answer that an author chose.
    REQUIRE(scene.SetPrimaryCamera(second));
    REQUIRE(scene.GetPrimaryCameraEntity() == second);
    REQUIRE(second.GetComponent<Forge::CameraComponent>().Primary);
    REQUIRE(first.GetComponent<Forge::CameraComponent>().Primary == false);

    // An entity without a camera cannot be made primary.
    Forge::Entity notACamera = scene.CreateEntity("Cube");
    REQUIRE_FALSE(scene.SetPrimaryCamera(notACamera));
    REQUIRE_FALSE(scene.SetPrimaryCamera(Forge::Entity()));
    REQUIRE(scene.GetPrimaryCameraEntity() == second);
}

TEST_CASE("A mesh component imports the model it names", "[scene][mesh]")
{
    Forge::Scene scene;

    // No path is a legitimate state - the entity may get geometry another way -
    // and is not an error.
    Forge::Entity empty = scene.CreateEntity("NoModel");
    auto& noModel = empty.AddComponent<Forge::MeshComponent>();
    REQUIRE(noModel.CpuMeshData == nullptr);

    // A path that does not resolve is reported, and leaves the component empty
    // rather than pretending it imported something.
    Forge::Entity missing = scene.CreateEntity("MissingModel");
    auto& badModel = missing.AddComponent<Forge::MeshComponent>();
    badModel.ModelPath = "no_such_model.gltf";
    REQUIRE(badModel.CpuMeshData == nullptr);

    // The real thing. The path is assigned after the component exists, so the
    // import is asked for explicitly - which is the same call a script makes
    // when it sets ModelPath at run time.
    Forge::Entity model = scene.CreateEntity("Model");
    auto& mesh = model.AddComponent<Forge::MeshComponent>();
    mesh.ModelPath = std::string(FORGE_SOURCE_DIR) + "/resources/models/two_parts.gltf";
    REQUIRE(scene.ImportMeshData(model));

    REQUIRE(mesh.CpuMeshData);
    REQUIRE(mesh.CpuMeshData->GetVertices().size() == 6u);
    REQUIRE(mesh.CpuMeshData->GetIndices().size() == 6u);
    for (uint32_t index : mesh.CpuMeshData->GetIndices())
        REQUIRE(index < mesh.CpuMeshData->GetVertices().size());
}

TEST_CASE("Setting a mesh path after the fact imports it too", "[scene][mesh]")
{
    // A script setting ModelPath goes through the component, so the engine has
    // to notice the change - otherwise the entity keeps the geometry it had (a
    // unit cube) and the path looks like it worked.
    Forge::Scene scene;
    Forge::Entity entity = scene.CreateEntity("LateModel");
    auto& mesh = entity.AddComponent<Forge::MeshComponent>();
    REQUIRE(mesh.CpuMeshData == nullptr);

    mesh.ModelPath = std::string(FORGE_SOURCE_DIR) + "/resources/models/cube.glb";
    REQUIRE(scene.ImportMeshData(entity));

    REQUIRE(mesh.CpuMeshData);
    REQUIRE(!mesh.CpuMeshData->GetVertices().empty());
}

// ---------------------------------------------------------------------------
// Ray picking (the Hazel SceneTree click-select query)
// ---------------------------------------------------------------------------

TEST_CASE("FindEntityOnRay picks the collider a ray hits", "[scene][picking]")
{
    Forge::Scene scene;

    Forge::Entity floor = scene.CreateEntity("Floor");
    floor.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, -1.0f, 0.0f };
    floor.AddComponent<Forge::BoxColliderComponent>().Size = { 10.0f, 1.0f, 10.0f };

    Forge::Entity plain = scene.CreateEntity("PlainEntity");
    plain.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 0.0f, -8.0f };

    // A ray straight down hits the floor.
    Forge::Entity hit = scene.FindEntityOnRay({ 0.0f, 5.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f);
    REQUIRE(hit);
    REQUIRE(hit.GetName() == "Floor");

    // A ray straight down through the entity with no collider: it passes
    // through its visual bounds and hits nothing, because the entity has no
    // collider - picking answers where things collide, not where they draw.
    Forge::Entity through = scene.FindEntityOnRay({ 0.0f, 5.0f, -8.0f }, { 0.0f, -1.0f, 0.0f }, 100.0f);
    REQUIRE(!through);
}

TEST_CASE("FindEntityOnRay picks the nearest hit, boxes and spheres alike", "[scene][picking]")
{
    Forge::Scene scene;

    Forge::Entity near = scene.CreateEntity("NearBox");
    near.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 0.0f, -3.0f };
    near.AddComponent<Forge::BoxColliderComponent>().Size = { 1.0f, 1.0f, 1.0f };

    Forge::Entity far = scene.CreateEntity("FarSphere");
    far.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 0.0f, -8.0f };
    far.AddComponent<Forge::SphereColliderComponent>().Radius = 1.0f;

    Forge::Entity hit = scene.FindEntityOnRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f);
    REQUIRE(hit);
    REQUIRE(hit.GetName() == "NearBox");

    // Past the near box, the sphere answers; remove the box and the same ray
    // runs on to it.
    scene.DestroyEntity(near);

    hit = scene.FindEntityOnRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f);
    REQUIRE(hit);
    REQUIRE(hit.GetName() == "FarSphere");
}

TEST_CASE("FindEntityOnRay respects maxDistance and misses cleanly", "[scene][picking]")
{
    Forge::Scene scene;

    Forge::Entity box = scene.CreateEntity("Box");
    box.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 0.0f, -5.0f };
    box.AddComponent<Forge::BoxColliderComponent>().Size = { 1.0f, 1.0f, 1.0f };

    // The box starts at -4.5 along the ray; 4 metres of reach cannot arrive
    // there, so nothing is hit.
    REQUIRE(!scene.FindEntityOnRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 4.0f));

    // The box is 5 away; 5.6 reaches it.
    REQUIRE(scene.FindEntityOnRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 5.6f));

    // Pointing away from it: never hit, at any distance.
    REQUIRE(!scene.FindEntityOnRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, 1000.0f));

    // Parallel to the box and off to the side: the slab test must not treat
    // the zero component as an infinite span.
    REQUIRE(!scene.FindEntityOnRay({ 10.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f));
}

TEST_CASE("FindEntityOnRay picks what was drawn, not only what collides", "[scene][picking]")
{
    Forge::Scene scene;

    // A mesh with no collider: the entity is drawn, so it can be clicked.
    // A scene whose objects have no colliders - which is most of what an
    // author makes before the physics is written - would otherwise be a
    // scene in which nothing can be selected.
    Forge::Entity drawn = scene.CreateEntity("DrawnCube");
    drawn.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 0.0f, -5.0f };
    drawn.AddComponent<Forge::MeshComponent>();

    Forge::Entity hit = scene.FindEntityOnRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f);
    REQUIRE(hit);
    REQUIRE(hit.GetName() == "DrawnCube");

    // The mesh's own bounds decide: a ray that passes beside the cube misses
    // it, and one that reaches it hits it.
    REQUIRE(!scene.FindEntityOnRay({ 5.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f));
    REQUIRE(scene.FindEntityOnRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 5.6f));
    REQUIRE(!scene.FindEntityOnRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 4.0f));

    // The nearer of two is the answer, whichever kind it is.
    Forge::Entity nearer = scene.CreateEntity("NearerCollider");
    nearer.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 0.0f, -2.0f };
    nearer.AddComponent<Forge::BoxColliderComponent>().Size = { 1.0f, 1.0f, 1.0f };

    hit = scene.FindEntityOnRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f);
    REQUIRE(hit);
    REQUIRE(hit.GetName() == "NearerCollider");
}

TEST_CASE("FindEntityOnRay reads the collider offset", "[scene][picking]")
{
    Forge::Scene scene;

    Forge::Entity box = scene.CreateEntity("OffsetBox");
    box.GetComponent<Forge::TransformComponent>().Translation = { 0.0f, 0.0f, -5.0f };
    auto& collider = box.AddComponent<Forge::BoxColliderComponent>();
    collider.Size = { 1.0f, 1.0f, 1.0f };
    collider.Offset = { 3.0f, 0.0f, 0.0f };

    // The shape is at x = 3, so a ray down x = 0 misses and a ray down x = 3
    // hits.
    REQUIRE(!scene.FindEntityOnRay({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f));
    REQUIRE(scene.FindEntityOnRay({ 3.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f));
}
