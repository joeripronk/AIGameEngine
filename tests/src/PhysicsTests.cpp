#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Physics/Physics.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Components.h"
#include "Forge/Core/Log.h"

TEST_CASE("Physics initializes", "[physics]")
{
    REQUIRE_NOTHROW(Forge::Log::Init());
    REQUIRE_NOTHROW(Forge::Physics::Init());
}

TEST_CASE("Physics shuts down", "[physics]")
{
    REQUIRE_NOTHROW(Forge::Log::Init());
    Forge::Physics::Init();
    REQUIRE_NOTHROW(Forge::Physics::Shutdown());
}

TEST_CASE("Physics init and shutdown multiple times", "[physics]")
{
    REQUIRE_NOTHROW(Forge::Log::Init());
    Forge::Physics::Init();
    Forge::Physics::Shutdown();
    Forge::Physics::Init();
    Forge::Physics::Shutdown();
}

TEST_CASE("RigidbodyComponent body types", "[physics][component]")
{
    REQUIRE((int)Forge::RigidbodyComponent::BodyType::Static == 0);
    REQUIRE((int)Forge::RigidbodyComponent::BodyType::Dynamic == 1);
    REQUIRE((int)Forge::RigidbodyComponent::BodyType::Kinematic == 2);
}

TEST_CASE("RigidbodyComponent default values", "[physics][component]")
{
    Forge::Scene scene;
    auto entity = scene.CreateEntity("Physics");
    auto& rb = entity.AddComponent<Forge::RigidbodyComponent>();

    REQUIRE(rb.Type == Forge::RigidbodyComponent::BodyType::Static);
    REQUIRE(rb.FixedRotation == false);
    REQUIRE(rb.RuntimeBody == nullptr);
}

TEST_CASE("BoxColliderComponent default values", "[physics][component]")
{
    Forge::Scene scene;
    auto entity = scene.CreateEntity("Collider");
    auto& bc = entity.AddComponent<Forge::BoxColliderComponent>();

    REQUIRE(bc.Offset == glm::vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(bc.Size == glm::vec3(1.0f, 1.0f, 1.0f));
    REQUIRE(bc.Density == Approx(1.0f));
    REQUIRE(bc.Friction == Approx(0.5f));
    REQUIRE(bc.Restitution == Approx(0.0f));
    REQUIRE(bc.RuntimeFixture == nullptr);
}

TEST_CASE("SphereColliderComponent default values", "[physics][component]")
{
    Forge::Scene scene;
    auto entity = scene.CreateEntity("Sphere");
    auto& sc = entity.AddComponent<Forge::SphereColliderComponent>();

    REQUIRE(sc.Offset == glm::vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(sc.Radius == Approx(0.5f));
    REQUIRE(sc.Density == Approx(1.0f));
    REQUIRE(sc.Friction == Approx(0.5f));
    REQUIRE(sc.Restitution == Approx(0.0f));
    REQUIRE(sc.RuntimeFixture == nullptr);
}

TEST_CASE("Scene physics start/stop", "[physics][scene]")
{
    Forge::Scene scene;
    REQUIRE_NOTHROW(scene.OnPhysicsStart());
    REQUIRE_NOTHROW(scene.OnPhysicsStop());
}

TEST_CASE("Scene simulation start/stop", "[physics][scene]")
{
    Forge::Scene scene;
    REQUIRE_NOTHROW(scene.OnSimulationStart());
    REQUIRE_NOTHROW(scene.OnSimulationStop());
}
