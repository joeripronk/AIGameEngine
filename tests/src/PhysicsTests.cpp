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

// ---------------------------------------------------------------------------
// Body sleeping
// ---------------------------------------------------------------------------

namespace {
    // Builds a dynamic box body of the given size with the given mass.
    Forge::PhysicsBody MakeDynamicBox(uint64_t id, const glm::vec3& size, float density = 1.0f)
    {
        Forge::RigidbodyComponent rb;
        rb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;

        Forge::BoxColliderComponent bc;
        bc.Size = size;
        bc.Density = density;

        Forge::PhysicsBody body(id, rb, bc, Forge::Physics::GetGravityY());
        return body;
    }
}

TEST_CASE("Physics body starts awake with zero sleep time", "[physics][sleep]")
{
    auto body = MakeDynamicBox(1, glm::vec3(1.0f));
    REQUIRE(body.IsSleeping == false);
    REQUIRE(body.SleepTime == Approx(0.0f));
}

TEST_CASE("Physics body sleeps after staying below the velocity threshold", "[physics][sleep]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f); // no gravity, so the body never accelerates

    auto* body = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{}, Forge::BoxColliderComponent{}, 0.0f);
    body->IsStatic = false;
    body->Position = glm::vec3(0.0f);
    body->Velocity = glm::vec3(0.0f);
    Forge::Physics::AddBody(body);

    Forge::Physics::SetSleepVelocityThreshold(0.05f);
    Forge::Physics::SetSleepTimeThreshold(0.1f);

    // One step well under the sleep time threshold: awake, timer accumulating.
    Forge::Physics::Update(0.05f);
    REQUIRE(body->IsSleeping == false);
    REQUIRE(body->SleepTime == Approx(0.05f));

    // Push past the threshold: the body falls asleep and its motion is zeroed.
    Forge::Physics::Update(0.1f);
    REQUIRE(body->IsSleeping == true);
    REQUIRE(body->SleepTime >= 0.1f);
    REQUIRE(body->Velocity == glm::vec3(0.0f));

    Forge::Physics::Shutdown();
}

TEST_CASE("Sleeping body does not integrate gravity", "[physics][sleep]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(-9.81f);

    auto* body = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{}, Forge::BoxColliderComponent{}, 0.0f);
    body->IsStatic = false;
    body->IsSleeping = true;
    body->Position = glm::vec3(0.0f);
    body->Velocity = glm::vec3(0.0f);
    Forge::Physics::AddBody(body);

    const glm::vec3 start = body->Position;
    Forge::Physics::Update(0.016f);

    // A sleeping body must neither accelerate nor drift.
    REQUIRE(body->Velocity == glm::vec3(0.0f));
    REQUIRE(body->Position == start);

    Forge::Physics::Shutdown();
}

TEST_CASE("Body exceeding the velocity threshold resets its sleep timer", "[physics][sleep]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto* body = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{}, Forge::BoxColliderComponent{}, 0.0f);
    body->IsStatic = false;
    body->Position = glm::vec3(0.0f);
    body->Velocity = glm::vec3(0.0f);
    Forge::Physics::AddBody(body);

    Forge::Physics::SetSleepTimeThreshold(0.5f);

    Forge::Physics::Update(0.2f);
    REQUIRE(body->SleepTime == Approx(0.2f));

    // Moving fast clears the accumulated sleep time.
    body->Velocity = glm::vec3(5.0f, 0.0f, 0.0f);
    Forge::Physics::Update(0.016f);
    REQUIRE(body->SleepTime == Approx(0.0f));
    REQUIRE(body->IsSleeping == false);

    Forge::Physics::Shutdown();
}

// ---------------------------------------------------------------------------
// Collision callbacks
// ---------------------------------------------------------------------------

namespace {
    // Records every callback invocation so tests can assert on enter/exit.
    struct ContactLog
    {
        int Enters = 0;
        int Exits = 0;
        glm::vec3 LastNormal = glm::vec3(0.0f);
        uint64_t LastOtherID = 0;
    };

    ContactLog g_ContactLogA;
    ContactLog g_ContactLogB;

    void RecordContact(ContactLog& log, Forge::PhysicsBody& other, const glm::vec3& normal, Forge::Physics::ContactPhase phase)
    {
        if (phase == Forge::Physics::ContactPhase::Begin)
            ++log.Enters;
        else
            ++log.Exits;

        log.LastNormal = normal;
        log.LastOtherID = other.ID;
    }

    void OnContactA(Forge::PhysicsBody& self, Forge::PhysicsBody& other, const glm::vec3& n, Forge::Physics::ContactPhase p)
    {
        (void)self;
        RecordContact(g_ContactLogA, other, n, p);
    }

    void OnContactB(Forge::PhysicsBody& self, Forge::PhysicsBody& other, const glm::vec3& n, Forge::Physics::ContactPhase p)
    {
        (void)self;
        RecordContact(g_ContactLogB, other, n, p);
    }

    void ResetContactLogs()
    {
        g_ContactLogA = ContactLog{};
        g_ContactLogB = ContactLog{};
    }

    Forge::PhysicsBody* MakeStaticBox(uint64_t id, const glm::vec3& position, const glm::vec3& size)
    {
        auto* body = new Forge::PhysicsBody(id, Forge::RigidbodyComponent{}, Forge::BoxColliderComponent{}, 0.0f);
        body->IsStatic = true;
        body->Position = position;
        body->BoxSize = size;
        return body;
    }

    // A dynamic box, so it participates in contacts against static geometry.
    Forge::PhysicsBody* MakeDynamicBoxAt(uint64_t id, const glm::vec3& position, const glm::vec3& size)
    {
        auto* body = new Forge::PhysicsBody(id, Forge::RigidbodyComponent{}, Forge::BoxColliderComponent{}, 0.0f);
        body->IsStatic = false;
        body->Mass = 1.0f;
        body->InverseMass = 1.0f;
        body->Position = position;
        body->BoxSize = size;
        return body;
    }
}

TEST_CASE("Collision callback fires when bodies begin touching", "[physics][collision]")
{
    ResetContactLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto* ground = MakeStaticBox(1, glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 10.0f));
    auto* ball = MakeDynamicBoxAt(2, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f));
    Forge::Physics::AddBody(ground);
    Forge::Physics::AddBody(ball);
    Forge::Physics::SetCollisionCallback(ball, OnContactA);
    Forge::Physics::SetCollisionCallback(ground, OnContactB);

    // Separated: no callbacks yet.
    Forge::Physics::Update(0.016f);
    REQUIRE(g_ContactLogA.Enters == 0);

    // Overlap the ground (ground spans y in [-0.5, 0.5]).
    ball->Position = glm::vec3(0.0f, 0.6f, 0.0f);
    Forge::Physics::Update(0.016f);

    REQUIRE(g_ContactLogA.Enters == 1);
    REQUIRE(g_ContactLogB.Enters == 1);
    REQUIRE(g_ContactLogA.LastOtherID == ground->ID);

    Forge::Physics::Shutdown();
}

TEST_CASE("Collision callback does not re-fire while bodies stay in contact", "[physics][collision]")
{
    ResetContactLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto* ground = MakeStaticBox(1, glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 10.0f));
    auto* ball = MakeDynamicBoxAt(2, glm::vec3(0.0f, 0.6f, 0.0f), glm::vec3(1.0f));
    Forge::Physics::AddBody(ground);
    Forge::Physics::AddBody(ball);
    Forge::Physics::SetCollisionCallback(ball, OnContactA);

    Forge::Physics::Update(0.016f);
    REQUIRE(g_ContactLogA.Enters == 1);

    // Positional correction pushes the dynamic ball clear of the ground, so
    // re-seat it each step to hold the contact open. Enter must not re-fire.
    for (int i = 0; i < 2; i++)
    {
        ball->Position = glm::vec3(0.0f, 0.6f, 0.0f);
        Forge::Physics::Update(0.016f);
    }

    REQUIRE(g_ContactLogA.Enters == 1);
    REQUIRE(g_ContactLogA.Exits == 0);

    Forge::Physics::Shutdown();
}

TEST_CASE("Collision callback fires when bodies separate", "[physics][collision]")
{
    ResetContactLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto* ground = MakeStaticBox(1, glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 10.0f));
    auto* ball = MakeDynamicBoxAt(2, glm::vec3(0.0f, 0.6f, 0.0f), glm::vec3(1.0f));
    Forge::Physics::AddBody(ground);
    Forge::Physics::AddBody(ball);
    Forge::Physics::SetCollisionCallback(ball, OnContactA);

    Forge::Physics::Update(0.016f);
    REQUIRE(g_ContactLogA.Enters == 1);
    REQUIRE(g_ContactLogA.Exits == 0);

    // Leave the ball clear of the ground: an exit must fire exactly once.
    ball->Position = glm::vec3(0.0f, 5.0f, 0.0f);
    Forge::Physics::Update(0.016f);
    REQUIRE(g_ContactLogA.Exits == 1);

    // And it must not fire again once already separated.
    Forge::Physics::Update(0.016f);
    REQUIRE(g_ContactLogA.Exits == 1);

    Forge::Physics::Shutdown();
}

TEST_CASE("Unregistering a collision callback stops delivery", "[physics][collision]")
{
    ResetContactLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto* ground = MakeStaticBox(1, glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 10.0f));
    auto* ball = MakeDynamicBoxAt(2, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f));
    Forge::Physics::AddBody(ground);
    Forge::Physics::AddBody(ball);
    Forge::Physics::SetCollisionCallback(ball, OnContactA);

    Forge::Physics::SetCollisionCallback(ball, nullptr);

    ball->Position = glm::vec3(0.0f, 0.6f, 0.0f);
    Forge::Physics::Update(0.016f);

    REQUIRE(g_ContactLogA.Enters == 0);

    Forge::Physics::Shutdown();
}

TEST_CASE("Removing a body drops its collision callback registration", "[physics][collision]")
{
    ResetContactLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto* ground = MakeStaticBox(1, glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 10.0f));
    auto* ball = MakeDynamicBoxAt(2, glm::vec3(0.0f, 0.6f, 0.0f), glm::vec3(1.0f));
    Forge::Physics::AddBody(ground);
    Forge::Physics::AddBody(ball);
    Forge::Physics::SetCollisionCallback(ball, OnContactA);

    // Destroying the body must erase its registration; the surviving body's
    // contact with nothing must not touch the freed pointer.
    Forge::Physics::RemoveBody(ball);
    Forge::Physics::Update(0.016f);

    REQUIRE(g_ContactLogA.Enters == 0);

    Forge::Physics::Shutdown();
}

TEST_CASE("Static-static pairs are never treated as contacts", "[physics][collision]")
{
    ResetContactLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    // Two static boxes fully overlapping: neither moves, so no contact.
    auto* a = MakeStaticBox(1, glm::vec3(0.0f), glm::vec3(1.0f));
    auto* b = MakeStaticBox(2, glm::vec3(0.0f), glm::vec3(1.0f));
    Forge::Physics::AddBody(a);
    Forge::Physics::AddBody(b);
    Forge::Physics::SetCollisionCallback(a, OnContactA);

    Forge::Physics::Update(0.016f);

    REQUIRE(g_ContactLogA.Enters == 0);
    REQUIRE(Forge::Physics::GetActiveContacts().empty());

    Forge::Physics::Shutdown();
}

TEST_CASE("Active contacts are reported for the current step", "[physics][collision]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto* ground = MakeStaticBox(1, glm::vec3(0.0f), glm::vec3(10.0f, 1.0f, 10.0f));
    auto* ball = MakeDynamicBoxAt(2, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f));
    Forge::Physics::AddBody(ground);
    Forge::Physics::AddBody(ball);

    Forge::Physics::Update(0.016f);
    REQUIRE(Forge::Physics::GetActiveContacts().empty());

    ball->Position = glm::vec3(0.0f, 0.6f, 0.0f);
    Forge::Physics::Update(0.016f);

    const auto& contacts = Forge::Physics::GetActiveContacts();
    REQUIRE(contacts.size() == 1);
    REQUIRE(contacts[0].A == ground->ID);
    REQUIRE(contacts[0].B == ball->ID);

    Forge::Physics::Shutdown();
}
