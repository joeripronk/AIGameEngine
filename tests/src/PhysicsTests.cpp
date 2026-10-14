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

TEST_CASE("PhysicsBody box collision detection", "[physics][collision]")
{
    Forge::Physics::Init();

    Forge::RigidbodyComponent rb1;
    rb1.Type = Forge::RigidbodyComponent::BodyType::Dynamic;

    Forge::BoxColliderComponent bc1;
    bc1.Size = glm::vec3(2.0f, 2.0f, 2.0f);
    bc1.Offset = glm::vec3(0.0f, 0.0f, 0.0f);

    Forge::PhysicsBody body1(1, rb1, bc1, Forge::Physics::GetGravityY());
    body1.Position = glm::vec3(0.0f, 0.0f, 0.0f);

    Forge::RigidbodyComponent rb2;
    rb2.Type = Forge::RigidbodyComponent::BodyType::Static;

    Forge::BoxColliderComponent bc2;
    bc2.Size = glm::vec3(2.0f, 2.0f, 2.0f);
    bc2.Offset = glm::vec3(0.0f, 0.0f, 0.0f);

    Forge::PhysicsBody body2(2, rb2, bc2, Forge::Physics::GetGravityY());
    body2.Position = glm::vec3(3.0f, 0.0f, 0.0f);

    // Bodies should not intersect (distance = 3, combined half-width = 2)
    REQUIRE(!body1.IntersectsAABB(body2));

    // Move body2 closer
    body2.Position = glm::vec3(1.5f, 0.0f, 0.0f);
    REQUIRE(body1.IntersectsAABB(body2));

    Forge::Physics::Shutdown();
}

TEST_CASE("PhysicsBody sphere collision detection", "[physics][collision]")
{
    Forge::PhysicsBody body1(1);
    body1.Shape = Forge::PhysicsBody::ShapeType::Sphere;
    body1.SphereRadius = 1.0f;
    body1.Position = glm::vec3(0.0f, 0.0f, 0.0f);

    Forge::PhysicsBody body2(2);
    body2.Shape = Forge::PhysicsBody::ShapeType::Sphere;
    body2.SphereRadius = 1.0f;
    body2.Position = glm::vec3(0.0f, 3.0f, 0.0f);

    // Bodies should not intersect (distance = 3, combined radius = 2)
    REQUIRE(!body1.IntersectsSphere(body2));

    // Move body2 closer
    body2.Position = glm::vec3(0.0f, 1.5f, 0.0f);
    REQUIRE(body1.IntersectsSphere(body2));
}

TEST_CASE("PhysicsBody AABB bounds", "[physics][collision]")
{
    Forge::RigidbodyComponent rb;
    rb.Type = Forge::RigidbodyComponent::BodyType::Static;

    Forge::BoxColliderComponent bc;
    bc.Size = glm::vec3(2.0f, 2.0f, 2.0f);
    bc.Offset = glm::vec3(0.0f, 0.0f, 0.0f);

    Forge::PhysicsBody body(1, rb, bc, 0.0f);
    body.Position = glm::vec3(0.0f, 0.0f, 0.0f);

    REQUIRE(body.GetAABBBottomLeft() == glm::vec3(-1.0f, -1.0f, -1.0f));
    REQUIRE(body.GetAABBBottomRight() == glm::vec3(1.0f, 1.0f, 1.0f));
}

TEST_CASE("PhysicsBody mass calculation", "[physics][mass]")
{
    Forge::RigidbodyComponent rb;
    rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;

    Forge::BoxColliderComponent bc;
    bc.Size = glm::vec3(1.0f, 1.0f, 1.0f);
    bc.Density = 2.0f;

    Forge::PhysicsBody body(1, rb, bc, 9.81f);
    REQUIRE(body.Mass == Approx(2.0f * 1.0f * 1.0f * 1.0f));
    REQUIRE(body.InverseMass == Approx(0.5f));
}

TEST_CASE("PhysicsBody sphere mass calculation", "[physics][mass]")
{
    Forge::RigidbodyComponent rb;
    rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;

    Forge::SphereColliderComponent sc;
    sc.Radius = 1.0f;
    sc.Density = 1.0f;

    Forge::PhysicsBody body(1, rb, sc, 9.81f);
    // Mass = density * (4/3) * pi * r^3 = 1 * 4.188... = 4.188...
    REQUIRE(body.Mass > 4.0f);
    REQUIRE(body.Mass < 4.2f);
}

TEST_CASE("PhysicsBody static body has zero inverse mass", "[physics][mass]")
{
    Forge::RigidbodyComponent rb;
    rb.Type = Forge::RigidbodyComponent::BodyType::Static;

    Forge::BoxColliderComponent bc;
    bc.Size = glm::vec3(1.0f, 1.0f, 1.0f);

    Forge::PhysicsBody body(1, rb, bc, 9.81f);
    REQUIRE(body.Mass == Approx(1.0f));
    REQUIRE(body.InverseMass == Approx(0.0f));
}

TEST_CASE("Physics gravity integration", "[physics][integration]")
{
    Forge::Physics::Init();
    REQUIRE(Forge::Physics::GetGravityY() == Approx(-9.81f));

    // Test that gravity is applied correctly using semi-implicit Euler
    // y = y0 + (v0 + g*dt)*dt = y0 + v0*dt + g*dt^2
    Forge::RigidbodyComponent rb;
    rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
    rb.FixedRotation = false;

    Forge::BoxColliderComponent bc;
    bc.Size = glm::vec3(1.0f, 1.0f, 1.0f);

    Forge::PhysicsBody body(1, rb, bc, Forge::Physics::GetGravityY());
    body.Position = glm::vec3(0.0f, 10.0f, 0.0f);
    body.Velocity = glm::vec3(0.0f, 0.0f, 0.0f);

    // Semi-implicit Euler: v += g*dt, then y += v*dt
    // y = 10 + (0 + (-9.81)*1)*1 = 10 - 9.81 = 0.19
    float timestep = 1.0f;
    body.Velocity.y += Forge::Physics::GetGravityY() * timestep;
    body.Position.y += body.Velocity.y * timestep;

    REQUIRE(body.Position.y == Approx(0.19f));

    // After second step: v = -9.81 + (-9.81)*1 = -19.62
    // y = 0.19 + (-19.62)*1 = -19.43
    body.Velocity.y += Forge::Physics::GetGravityY() * timestep;
    body.Position.y += body.Velocity.y * timestep;

    REQUIRE(body.Position.y == Approx(-19.43f));

    Forge::Physics::Shutdown();
}
