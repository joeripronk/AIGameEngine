#include <catch2/catch_test_macros.hpp>

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"

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
