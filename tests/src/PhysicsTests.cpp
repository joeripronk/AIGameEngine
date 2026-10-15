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

// ---------------------------------------------------------------------------
// Scene-level entity collision handlers
// ---------------------------------------------------------------------------

namespace {
    struct SceneContactLog
    {
        int Begins = 0;
        int Ends = 0;
        uint64_t SelfID = 0;
        uint64_t OtherID = 0;
        bool OtherWasValid = false;
        glm::vec3 LastNormal = glm::vec3(0.0f);
    };

    SceneContactLog g_SceneContactA;
    SceneContactLog g_SceneContactB;

    void SceneRecord(SceneContactLog& log, Forge::Entity self, Forge::Entity other, const glm::vec3& n, Forge::Physics::ContactPhase phase)
    {
        if (phase == Forge::Physics::ContactPhase::Begin)
            ++log.Begins;
        else
            ++log.Ends;

        log.SelfID = self.GetUUID();
        log.OtherWasValid = static_cast<bool>(other);
        log.OtherID = other ? other.GetUUID() : Forge::UUID();
        log.LastNormal = n;
    }

    void SceneOnA(Forge::Entity self, Forge::Entity other, const glm::vec3& n, Forge::Physics::ContactPhase p) { SceneRecord(g_SceneContactA, self, other, n, p); }
    void SceneOnB(Forge::Entity self, Forge::Entity other, const glm::vec3& n, Forge::Physics::ContactPhase p) { SceneRecord(g_SceneContactB, self, other, n, p); }

    void ResetSceneLogs()
    {
        g_SceneContactA = SceneContactLog{};
        g_SceneContactB = SceneContactLog{};
    }

    // Builds a static ground plane and a dynamic box resting just above it.
    void BuildCollisionPair(Forge::Scene& scene, Forge::Entity& ground, Forge::Entity& ball)
    {
        ground = scene.CreateEntity("Ground");

        auto& groundRb = ground.AddComponent<Forge::RigidbodyComponent>();
        groundRb.Type = Forge::RigidbodyComponent::BodyType::Static;
        auto& groundCollider = ground.AddComponent<Forge::BoxColliderComponent>();
        groundCollider.Size = glm::vec3(10.0f, 1.0f, 10.0f);
        ground.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f);

        ball = scene.CreateEntity("Ball");

        auto& ballRb = ball.AddComponent<Forge::RigidbodyComponent>();
        ballRb.Type = Forge::RigidbodyComponent::BodyType::Dynamic;
        auto& ballCollider = ball.AddComponent<Forge::BoxColliderComponent>();
        ballCollider.Size = glm::vec3(1.0f);
        ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 5.0f, 0.0f);
    }
}

TEST_CASE("Scene forwards collision events to entity handlers", "[physics][scene-collision]")
{
    ResetSceneLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    Forge::Scene scene;
    Forge::Entity ground, ball;
    BuildCollisionPair(scene, ground, ball);

    scene.SetCollisionHandler(ball, SceneOnA);
    scene.SetCollisionHandler(ground, SceneOnB);

    scene.OnPhysicsStart();
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));

    // Still separated.
    REQUIRE(g_SceneContactA.Begins == 0);

    // Drop the ball onto the ground.
    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 0.6f, 0.0f);
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));

    REQUIRE(g_SceneContactA.Begins == 1);
    REQUIRE(g_SceneContactB.Begins == 1);

    // Each handler receives the correct partner.
    REQUIRE(g_SceneContactA.SelfID == ball.GetUUID());
    REQUIRE(g_SceneContactA.OtherWasValid == true);
    REQUIRE(g_SceneContactA.OtherID == ground.GetUUID());

    REQUIRE(g_SceneContactB.SelfID == ground.GetUUID());
    REQUIRE(g_SceneContactB.OtherID == ball.GetUUID());

    scene.OnPhysicsStop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Scene collision handlers fire on separation", "[physics][scene-collision]")
{
    ResetSceneLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    Forge::Scene scene;
    Forge::Entity ground, ball;
    BuildCollisionPair(scene, ground, ball);

    scene.SetCollisionHandler(ball, SceneOnA);
    scene.OnPhysicsStart();

    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 0.6f, 0.0f);
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));
    REQUIRE(g_SceneContactA.Begins == 1);
    REQUIRE(g_SceneContactA.Ends == 0);

    // Move the ball away: an end event must fire.
    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 5.0f, 0.0f);
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));
    REQUIRE(g_SceneContactA.Ends == 1);

    scene.OnUpdateSimulation(Forge::Timestep(0.016f));
    REQUIRE(g_SceneContactA.Ends == 1);

    scene.OnPhysicsStop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Scene collision handler registered before playback still fires", "[physics][scene-collision]")
{
    ResetSceneLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    Forge::Scene scene;
    Forge::Entity ground, ball;
    BuildCollisionPair(scene, ground, ball);

    // Registered before OnPhysicsStart, so the handler must be attached to the
    // body as it is created rather than being silently dropped.
    scene.SetCollisionHandler(ball, SceneOnA);
    scene.OnPhysicsStart();

    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 0.6f, 0.0f);
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));

    REQUIRE(g_SceneContactA.Begins == 1);

    scene.OnPhysicsStop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Scene collision handler registered mid-simulation fires", "[physics][scene-collision]")
{
    ResetSceneLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    Forge::Scene scene;
    Forge::Entity ground, ball;
    BuildCollisionPair(scene, ground, ball);

    scene.OnPhysicsStart();

    // Register after the bodies exist: must bind immediately.
    scene.SetCollisionHandler(ball, SceneOnA);

    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 0.6f, 0.0f);
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));

    REQUIRE(g_SceneContactA.Begins == 1);

    scene.OnPhysicsStop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Unregistering a Scene collision handler stops events", "[physics][scene-collision]")
{
    ResetSceneLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    Forge::Scene scene;
    Forge::Entity ground, ball;
    BuildCollisionPair(scene, ground, ball);

    scene.SetCollisionHandler(ball, SceneOnA);
    scene.OnPhysicsStart();
    scene.SetCollisionHandler(ball, nullptr);

    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 0.6f, 0.0f);
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));

    REQUIRE(g_SceneContactA.Begins == 0);

    scene.OnPhysicsStop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Scene collision handler survives a stop and restart of playback", "[physics][scene-collision]")
{
    ResetSceneLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    Forge::Scene scene;
    Forge::Entity ground, ball;
    BuildCollisionPair(scene, ground, ball);

    scene.SetCollisionHandler(ball, SceneOnA);

    scene.OnPhysicsStart();
    scene.OnPhysicsStop();

    // Handlers live on the Scene, so a second run must re-attach them to the
    // freshly created bodies.
    scene.OnPhysicsStart();
    ball.GetComponent<Forge::TransformComponent>().Translation = glm::vec3(0.0f, 0.6f, 0.0f);
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));

    REQUIRE(g_SceneContactA.Begins == 1);

    scene.OnPhysicsStop();
    Forge::Physics::Shutdown();
}

TEST_CASE("Destroying an entity mid-simulation is safe", "[physics][scene-collision]")
{
    ResetSceneLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    Forge::Scene scene;
    Forge::Entity ground, ball;
    BuildCollisionPair(scene, ground, ball);

    scene.SetCollisionHandler(ground, SceneOnB);
    scene.OnPhysicsStart();

    // Removing the partner mid-simulation must not leave a dangling body or
    // crash the dispatcher on the next step.
    scene.DestroyEntity(ball);
    scene.OnUpdateSimulation(Forge::Timestep(0.016f));

    REQUIRE(g_SceneContactB.Begins == 0);

    scene.OnPhysicsStop();
    Forge::Physics::Shutdown();
}

// ---------------------------------------------------------------------------
// Kinematic bodies
// ---------------------------------------------------------------------------

namespace {
    Forge::PhysicsBody MakeKinematicBox(uint64_t id, const glm::vec3& size)
    {
        Forge::RigidbodyComponent rb;
        rb.Type = Forge::RigidbodyComponent::BodyType::Kinematic;

        Forge::BoxColliderComponent bc;
        bc.Size = size;
        bc.Density = 1.0f;

        Forge::PhysicsBody body(id, rb, bc, Forge::Physics::GetGravityY());
        return body;
    }
}

TEST_CASE("Kinematic body is flagged and has infinite mass", "[physics][kinematic]")
{
    Forge::Physics::Init();

    auto body = MakeKinematicBox(1, glm::vec3(1.0f));

    REQUIRE(body.IsStatic == false);
    REQUIRE(body.IsKinematic == true);
    REQUIRE(body.InverseMass == Approx(0.0f));
}

TEST_CASE("Kinematic body ignores gravity", "[physics][kinematic]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(-9.81f);

    auto* body = new Forge::PhysicsBody(3, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, Forge::Physics::GetGravityY());
    body->IsStatic = false;
    body->IsKinematic = true;
    body->Position = glm::vec3(0.0f);
    body->Velocity = glm::vec3(0.0f);
    Forge::Physics::AddBody(body);

    for (int i = 0; i < 10; i++)
        Forge::Physics::Update(0.016f);

    REQUIRE(body->Velocity == glm::vec3(0.0f));
    REQUIRE(body->Position == glm::vec3(0.0f));

    Forge::Physics::Shutdown();
}

TEST_CASE("Kinematic body advances by its velocity", "[physics][kinematic]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto* body = new Forge::PhysicsBody(3, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    body->IsStatic = false;
    body->IsKinematic = true;
    body->Position = glm::vec3(0.0f);
    body->Velocity = glm::vec3(2.0f, 0.0f, 0.0f);
    Forge::Physics::AddBody(body);

    Forge::Physics::Update(1.0f);

    REQUIRE(body->Position.x == Approx(2.0f));

    // Velocity is consumed by the step, so the body does not run away.
    REQUIRE(body->Velocity == glm::vec3(0.0f));

    Forge::Physics::Shutdown();
}

TEST_CASE("Kinematic body is not pushed by a dynamic body", "[physics][kinematic]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto* platform = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    platform->IsStatic = false;
    platform->IsKinematic = true;
    platform->InverseMass = 0.0f;
    platform->Position = glm::vec3(0.0f);
    platform->BoxSize = glm::vec3(10.0f, 1.0f, 10.0f);

    auto* crate = new Forge::PhysicsBody(2, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    crate->IsStatic = false;
    crate->InverseMass = 1.0f;
    crate->Position = glm::vec3(0.0f, 0.6f, 0.0f);
    crate->BoxSize = glm::vec3(1.0f);

    Forge::Physics::AddBody(platform);
    Forge::Physics::AddBody(crate);

    const glm::vec3 platformStart = platform->Position;

    Forge::Physics::Update(0.016f);

    // The platform must not move at all; the crate absorbs the whole correction.
    REQUIRE(platform->Position == platformStart);
    REQUIRE(crate->Position.y > 0.6f);

    Forge::Physics::Shutdown();
}

TEST_CASE("Positional correction is weighted by inverse mass", "[physics][collision]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    // A heavy box and a light box overlapping: the light one must move roughly
    // twice as far as the heavy one (inverse mass 1 vs 0.5).
    auto heavy = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    heavy->IsStatic = false;
    heavy->Mass = 2.0f;
    heavy->InverseMass = 0.5f;
    heavy->Position = glm::vec3(0.0f);
    heavy->BoxSize = glm::vec3(1.0f);

    auto light = new Forge::PhysicsBody(2, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    light->IsStatic = false;
    light->Mass = 1.0f;
    light->InverseMass = 1.0f;
    light->Position = glm::vec3(0.9f, 0.0f, 0.0f);
    light->BoxSize = glm::vec3(1.0f);

    Forge::Physics::AddBody(heavy);
    Forge::Physics::AddBody(light);

    Forge::Physics::Update(0.016f);

    // The light box is displaced twice as far as the heavy one.
    const float lightDisplacement = std::abs(light->Position.x - 0.9f);
    const float heavyDisplacement = std::abs(heavy->Position.x - 0.0f);

    REQUIRE(lightDisplacement > heavyDisplacement);
    REQUIRE(lightDisplacement == Approx(heavyDisplacement * 2.0f).margin(0.001f));

    Forge::Physics::Shutdown();
}

TEST_CASE("Kinematic body never sleeps", "[physics][kinematic][sleep]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::Physics::SetSleepTimeThreshold(0.01f);

    auto* body = new Forge::PhysicsBody(3, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    body->IsStatic = false;
    body->IsKinematic = true;
    body->Position = glm::vec3(0.0f);
    Forge::Physics::AddBody(body);

    for (int i = 0; i < 20; i++)
        Forge::Physics::Update(0.016f);

    REQUIRE(body->IsSleeping == false);

    Forge::Physics::Shutdown();
}

TEST_CASE("Kinematic body reports contacts like a static one", "[physics][kinematic][collision]")
{
    ResetContactLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto platform = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    platform->IsStatic = false;
    platform->IsKinematic = true;
    platform->InverseMass = 0.0f;
    platform->Position = glm::vec3(0.0f);
    platform->BoxSize = glm::vec3(10.0f, 1.0f, 10.0f);
    Forge::Physics::AddBody(platform);

    auto* ball = MakeDynamicBoxAt(2, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f));
    Forge::Physics::AddBody(ball);
    Forge::Physics::SetCollisionCallback(ball, OnContactA);

    Forge::Physics::Update(0.016f);
    REQUIRE(g_ContactLogA.Enters == 0);

    ball->Position = glm::vec3(0.0f, 0.6f, 0.0f);
    Forge::Physics::Update(0.016f);

    // A kinematic/static pair still counts as a contact for the dynamic side.
    REQUIRE(g_ContactLogA.Enters == 1);

    Forge::Physics::Shutdown();
}

// ---------------------------------------------------------------------------
// Resting contact regression
//
// These cover a bug where the collision normal was inverted, so overlapping
// bodies were pushed further into each other instead of apart: a box dropped
// on the ground sank through it.
// ---------------------------------------------------------------------------

TEST_CASE("A box dropped on the ground settles on top of it", "[physics][resting]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(-9.81f);

    auto* ground = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    ground->IsStatic = true;
    ground->Position = glm::vec3(0.0f);
    ground->BoxSize = glm::vec3(20.0f, 1.0f, 20.0f);

    auto* box = new Forge::PhysicsBody(2, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    box->IsStatic = false;
    box->Mass = 1.0f;
    box->InverseMass = 1.0f;
    box->Position = glm::vec3(0.0f, 3.0f, 0.0f);
    box->BoxSize = glm::vec3(1.0f);

    Forge::Physics::AddBody(ground);
    Forge::Physics::AddBody(box);

    // Simulate a couple of seconds of falling.
    for (int i = 0; i < 120; i++)
        Forge::Physics::Update(1.0f / 60.0f);

    // Ground surface is at y = 0.5; a unit box resting on it has its centre
    // at y = 1.0 and must never sink below the surface.
    REQUIRE(box->Position.y > 0.5f);
    REQUIRE(box->Position.y < 1.2f);

    Forge::Physics::Shutdown();
}

TEST_CASE("A box dropped on the ground stops falling", "[physics][resting]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(-9.81f);

    auto* ground = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    ground->IsStatic = true;
    ground->Position = glm::vec3(0.0f);
    ground->BoxSize = glm::vec3(20.0f, 1.0f, 20.0f);

    auto* box = new Forge::PhysicsBody(2, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    box->IsStatic = false;
    box->Mass = 1.0f;
    box->InverseMass = 1.0f;
    box->Position = glm::vec3(0.0f, 3.0f, 0.0f);
    box->BoxSize = glm::vec3(1.0f);
    box->Restitution = 0.0f;

    Forge::Physics::AddBody(ground);
    Forge::Physics::AddBody(box);

    for (int i = 0; i < 240; i++)
        Forge::Physics::Update(1.0f / 60.0f);

    // Given time, the box must have come to rest rather than accumulating speed.
    REQUIRE(box->Position.y > 0.5f);
    REQUIRE(std::abs(box->Velocity.y) < 1.0f);

    Forge::Physics::Shutdown();
}

TEST_CASE("Overlapping bodies are pushed apart, not together", "[physics][resting]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);

    auto* a = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    a->IsStatic = false;
    a->Mass = 1.0f;
    a->InverseMass = 1.0f;
    a->Position = glm::vec3(0.0f);
    a->BoxSize = glm::vec3(1.0f);

    auto* b = new Forge::PhysicsBody(2, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    b->IsStatic = false;
    b->Mass = 1.0f;
    b->InverseMass = 1.0f;
    // Overlapping A by 0.4 along x.
    b->Position = glm::vec3(0.6f, 0.0f, 0.0f);
    b->BoxSize = glm::vec3(1.0f);

    Forge::Physics::AddBody(a);
    Forge::Physics::AddBody(b);

    const float initialSeparation = b->Position.x - a->Position.x;

    Forge::Physics::Update(0.016f);

    // The overlap must shrink, not grow.
    REQUIRE(b->Position.x - a->Position.x > initialSeparation);

    Forge::Physics::Shutdown();
}

// ---------------------------------------------------------------------------
// Continuous collision detection
// ---------------------------------------------------------------------------

namespace {
    // A static wall of the given half-extent, centred on the origin.
    Forge::PhysicsBody* MakeWall(uint64_t id, float halfThickness)
    {
        auto* wall = new Forge::PhysicsBody(id, Forge::RigidbodyComponent{},
            Forge::BoxColliderComponent{}, 0.0f);
        wall->IsStatic = true;
        wall->Position = glm::vec3(0.0f);
        wall->BoxSize = glm::vec3(halfThickness, 50.0f, 50.0f);
        return wall;
    }

    // A small dynamic box launched along +X at the given speed. `startX` places it
    // so the bullet crosses the wall within a single 60Hz frame.
    Forge::PhysicsBody* MakeBullet(uint64_t id, float speed, float size = 0.1f, float startX = -1.0f)
    {
        auto* bullet = new Forge::PhysicsBody(id, Forge::RigidbodyComponent{},
            Forge::BoxColliderComponent{}, 0.0f);
        bullet->IsStatic = false;
        bullet->Mass = 1.0f;
        bullet->InverseMass = 1.0f;
        bullet->Position = glm::vec3(startX, 0.0f, 0.0f);
        bullet->Velocity = glm::vec3(speed, 0.0f, 0.0f);
        bullet->BoxSize = glm::vec3(size);
        bullet->Restitution = 0.0f;
        return bullet;
    }
}

TEST_CASE("A fast body tunnels through a thin wall without CCD", "[physics][ccd]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::Physics::SetContinuousCollision(false);

    // A 0.05-thick wall and a 0.1 bullet at 200 m/s travel 3.33 m per step,
    // so discrete detection never sees the overlap.
    auto* wall = MakeWall(1, 0.025f);
    auto* bullet = MakeBullet(2, 200.0f);
    Forge::Physics::AddBody(wall);
    Forge::Physics::AddBody(bullet);

    Forge::Physics::Update(1.0f / 60.0f);

    // Documented discrete-detection limitation: the bullet ends up past the wall.
    REQUIRE(Forge::Physics::GetLastSubstepCount() == 1);
    REQUIRE(bullet->Position.x > 0.0f);

    Forge::Physics::Shutdown();
}

TEST_CASE("A fast body does not tunnel through a thin wall with CCD", "[physics][ccd]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::Physics::SetContinuousCollision(true);
    Forge::Physics::SetMaxSubsteps(64);

    auto* wall = MakeWall(1, 0.025f);
    auto* bullet = MakeBullet(2, 200.0f);
    Forge::Physics::AddBody(wall);
    Forge::Physics::AddBody(bullet);

    Forge::Physics::Update(1.0f / 60.0f);

    // The bullet must be stopped at the near face of the wall, never beyond it.
    REQUIRE(Forge::Physics::GetLastSubstepCount() > 1);
    REQUIRE(bullet->Position.x < 0.0f);
    REQUIRE(bullet->Position.x > -0.5f);

    Forge::Physics::Shutdown();
}

TEST_CASE("CCD keeps a fast body from tunnelling through a sphere", "[physics][ccd]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::Physics::SetContinuousCollision(true);
    Forge::Physics::SetMaxSubsteps(64);

    auto* ball = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{},
        Forge::SphereColliderComponent{}, 0.0f);
    ball->IsStatic = true;
    ball->Position = glm::vec3(0.0f);
    ball->SphereRadius = 0.05f;

    auto* bullet = MakeBullet(2, 200.0f);
    Forge::Physics::AddBody(ball);
    Forge::Physics::AddBody(bullet);

    Forge::Physics::Update(1.0f / 60.0f);

    REQUIRE(bullet->Position.x < 0.0f);

    Forge::Physics::Shutdown();
}

TEST_CASE("CCD costs nothing when nothing is moving", "[physics][ccd]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::Physics::SetContinuousCollision(true);

    auto* a = new Forge::PhysicsBody(1, Forge::RigidbodyComponent{},
        Forge::BoxColliderComponent{}, 0.0f);
    a->IsStatic = false;
    a->Position = glm::vec3(0.0f);
    a->BoxSize = glm::vec3(1.0f);

    Forge::Physics::AddBody(a);

    Forge::Physics::Update(1.0f / 60.0f);

    // A stationary scene must stay at a single substep.
    REQUIRE(Forge::Physics::GetLastSubstepCount() == 1);

    Forge::Physics::Shutdown();
}

TEST_CASE("CCD substep count scales with speed", "[physics][ccd]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::Physics::SetContinuousCollision(true);
    Forge::Physics::SetMaxSubsteps(64);

    // A 0.1-thick box moving 0.167 m per step needs more substeps than one moving
    // 0.5 m per step (half the thickness is the per-substep budget).
    auto* slow = MakeBullet(1, 10.0f);
    Forge::Physics::AddBody(slow);
    Forge::Physics::Update(1.0f / 60.0f);
    const uint32_t slowSubsteps = Forge::Physics::GetLastSubstepCount();

    Forge::Physics::Shutdown();

    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::Physics::SetContinuousCollision(true);
    Forge::Physics::SetMaxSubsteps(64);

    auto* fast = MakeBullet(1, 30.0f);
    Forge::Physics::AddBody(fast);
    Forge::Physics::Update(1.0f / 60.0f);
    const uint32_t fastSubsteps = Forge::Physics::GetLastSubstepCount();

    REQUIRE(slowSubsteps > 1);
    REQUIRE(fastSubsteps > slowSubsteps);

    Forge::Physics::Shutdown();
}

TEST_CASE("CCD substeps never exceed the configured maximum", "[physics][ccd]")
{
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::Physics::SetContinuousCollision(true);
    Forge::Physics::SetMaxSubsteps(4);

    auto* bullet = MakeBullet(1, 5000.0f);
    Forge::Physics::AddBody(bullet);

    Forge::Physics::Update(1.0f / 60.0f);

    REQUIRE(Forge::Physics::GetLastSubstepCount() <= 4);

    Forge::Physics::Shutdown();
}

TEST_CASE("CCD reports a collision only once across substeps", "[physics][ccd]")
{
    ResetContactLogs();
    Forge::Physics::Init();
    Forge::Physics::SetGravityY(0.0f);
    Forge::Physics::SetContinuousCollision(true);
    Forge::Physics::SetMaxSubsteps(32);

    // A 1.0-thick wall (half-extent 0.5) and a 0.2 bullet at 100 m/s covers
    // 1.67 m per frame, so the bullet crosses the wall mid-frame.
    auto* wall = MakeWall(1, 0.5f);
    auto* bullet = MakeBullet(2, 100.0f, 0.2f, -1.5f);
    Forge::Physics::AddBody(wall);
    Forge::Physics::AddBody(bullet);
    Forge::Physics::SetCollisionCallback(bullet, OnContactA);

    Forge::Physics::Update(1.0f / 60.0f);

    // A contact spanning several substeps must still be a single Begin event,
    // otherwise callbacks would fire many times for one impact.
    REQUIRE(Forge::Physics::GetLastSubstepCount() > 1);
    REQUIRE(g_ContactLogA.Enters == 1);

    Forge::Physics::Shutdown();
}

TEST_CASE("CCD defaults to disabled and can be toggled", "[physics][ccd]")
{
    Forge::Physics::Init();
    REQUIRE(Forge::Physics::GetContinuousCollision() == false);

    Forge::Physics::SetContinuousCollision(true);
    REQUIRE(Forge::Physics::GetContinuousCollision() == true);

    Forge::Physics::SetContinuousCollision(false);
    REQUIRE(Forge::Physics::GetContinuousCollision() == false);

    Forge::Physics::Shutdown();
}
