#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
using Catch::Approx;

#include "Forge/Renderer/Camera.h"
#include "Forge/Renderer/RendererStats.h"
#include "Forge/Math/Math.h"
#include "Forge/Scene/Components.h"

#include <utility>

using namespace Forge;

// ---------------------------------------------------------------------------
// Frustum corners (Hazel's method: unproject the NDC cube)
// ---------------------------------------------------------------------------

TEST_CASE("Camera frustum corners unproject the near and far NDC planes", "[renderer][frustum]")
{
    // A 90-degree perspective with aspect 1 at the origin looking along -Z:
    // the view matrix is the identity, so the world-space corners are known
    // exactly - half-width tan(45) times the depth on each plane.
    Camera camera(90.0f, 1.0f, 1.0f);
    camera.SetProjection(90.0f, 1.0f, 1.0f, 10.0f);
    camera.SetPosition({ 0.0f, 0.0f, 0.0f });
    camera.SetRotation({ 0.0f, 0.0f, 0.0f });

    const FrustumCorners corners = camera.GetFrustumCorners();

    REQUIRE(corners[(int)FrustumTypes::TopLeft].x == Approx(-1.0f));
    REQUIRE(corners[(int)FrustumTypes::TopLeft].y == Approx(1.0f));
    REQUIRE(corners[(int)FrustumTypes::TopLeft].z == Approx(-1.0f));

    REQUIRE(corners[(int)FrustumTypes::BottomRight].x == Approx(1.0f));
    REQUIRE(corners[(int)FrustumTypes::BottomRight].z == Approx(-1.0f));

    REQUIRE(corners[(int)FrustumTypes::FarTopLeft].x == Approx(-10.0f));
    REQUIRE(corners[(int)FrustumTypes::FarTopLeft].y == Approx(10.0f));
    REQUIRE(corners[(int)FrustumTypes::FarTopLeft].z == Approx(-10.0f));
    REQUIRE(corners[(int)FrustumTypes::FarBottomRight].z == Approx(-10.0f));
}

TEST_CASE("Frustum point test answers the six planes", "[renderer][frustum]")
{
    Camera camera(90.0f, 1.0f, 1.0f);
    camera.SetProjection(90.0f, 1.0f, 1.0f, 10.0f);
    camera.SetPosition({ 0.0f, 0.0f, 0.0f });
    camera.SetRotation({ 0.0f, 0.0f, 0.0f });

    const FrustumCorners corners = camera.GetFrustumCorners();

    // Straight ahead, between the clips: inside.
    REQUIRE(Frustum::IntersectsPoint(corners, { 0.0f, 0.0f, -5.0f }));
    REQUIRE(Frustum::IntersectsPoint(corners, { 0.0f, 0.0f, -2.0f }));
    REQUIRE(Frustum::IntersectsPoint(corners, { 0.0f, 0.0f, -9.0f }));

    // In front of the near clip: the camera is inside it - nothing there is
    // ever visible, and drawing it is the waste culling exists to remove.
    REQUIRE_FALSE(Frustum::IntersectsPoint(corners, { 0.0f, 0.0f, -0.5f }));
    REQUIRE_FALSE(Frustum::IntersectsPoint(corners, { 0.0f, 0.0f, 3.0f }));

    // Past the far clip.
    REQUIRE_FALSE(Frustum::IntersectsPoint(corners, { 0.0f, 0.0f, -11.0f }));

    // At depth -5 the half-width of this frustum is 5: clearly inside, and
    // clearly outside. (A point exactly on a side plane is the boundary the
    // float answer is least meaningful at, so it is not asserted.)
    REQUIRE(Frustum::IntersectsPoint(corners, { 4.0f, 0.0f, -5.0f }));
    REQUIRE_FALSE(Frustum::IntersectsPoint(corners, { 6.0f, 0.0f, -5.0f }));
    REQUIRE_FALSE(Frustum::IntersectsPoint(corners, { 0.0f, 6.0f, -5.0f }));
}

TEST_CASE("Frustum AABB test keeps straddling boxes and culls proven misses", "[renderer][frustum]")
{
    Camera camera(90.0f, 1.0f, 1.0f);
    camera.SetProjection(90.0f, 1.0f, 1.0f, 10.0f);
    camera.SetPosition({ 0.0f, 0.0f, 0.0f });
    camera.SetRotation({ 0.0f, 0.0f, 0.0f });

    const FrustumCorners corners = camera.GetFrustumCorners();

    auto box = [](glm::vec3 center, glm::vec3 half)
    {
        return std::make_pair(center - half, center + half);
    };

    // Wholly inside.
    {
        const auto [min, max] = box({ 0.0f, 0.0f, -5.0f }, { 1.0f, 1.0f, 1.0f });
        REQUIRE(Frustum::IntersectsAABB(corners, min, max));
    }

    // Wholly behind the camera.
    {
        const auto [min, max] = box({ 0.0f, 0.0f, 3.0f }, { 1.0f, 1.0f, 1.0f });
        REQUIRE_FALSE(Frustum::IntersectsAABB(corners, min, max));
    }

    // Wholly past the far clip.
    {
        const auto [min, max] = box({ 0.0f, 0.0f, -20.0f }, { 1.0f, 1.0f, 1.0f });
        REQUIRE_FALSE(Frustum::IntersectsAABB(corners, min, max));
    }

    // Straddling the near plane: part of it is visible, so it is not a
    // provable miss and must survive culling.
    {
        const auto [min, max] = box({ 0.0f, 0.0f, -0.5f }, { 0.6f, 0.6f, 0.6f });
        REQUIRE(Frustum::IntersectsAABB(corners, min, max));
    }

    // Off to the side but crossing the side plane - same rule.
    {
        const auto [min, max] = box({ 5.5f, 0.0f, -5.0f }, { 1.0f, 1.0f, 1.0f });
        REQUIRE(Frustum::IntersectsAABB(corners, min, max));
    }

    // Wholly past the right plane at every depth it spans.
    {
        const auto [min, max] = box({ 30.0f, 0.0f, -5.0f }, { 1.0f, 1.0f, 1.0f });
        REQUIRE_FALSE(Frustum::IntersectsAABB(corners, min, max));
    }
}

TEST_CASE("Frustum follows the camera position and rotation", "[renderer][frustum]")
{
    // Same geometry, camera moved behind the origin and looking the same way.
    Camera camera(90.0f, 1.0f, 1.0f);
    camera.SetProjection(90.0f, 1.0f, 1.0f, 10.0f);
    camera.SetPosition({ 0.0f, 0.0f, -10.0f });
    camera.SetRotation({ 0.0f, 0.0f, 0.0f });

    FrustumCorners corners = camera.GetFrustumCorners();

    // 5 units in front of the camera (world -15), inside.
    REQUIRE(Frustum::IntersectsPoint(corners, { 0.0f, 0.0f, -15.0f }));
    // The old frustum's inside point is now behind the camera.
    REQUIRE_FALSE(Frustum::IntersectsPoint(corners, { 0.0f, 0.0f, -5.0f }));

    // Turn the camera 180 degrees: it now looks towards +Z, so the previous
    // "behind" point is ahead of it and the "ahead" point is behind it.
    camera.SetPosition({ 0.0f, 0.0f, -10.0f });
    camera.SetRotation({ 0.0f, 180.0f, 0.0f });
    corners = camera.GetFrustumCorners();

    REQUIRE(Frustum::IntersectsPoint(corners, { 0.0f, 0.0f, -5.0f }));
    REQUIRE_FALSE(Frustum::IntersectsPoint(corners, { 0.0f, 0.0f, -15.0f }));
}

// ---------------------------------------------------------------------------
// Draw statistics (the Hazel Renderer2D::Statistics pattern)
// ---------------------------------------------------------------------------

TEST_CASE("Renderer statistics accumulate and reset", "[renderer][stats]")
{
    RendererStatistics stats;

    REQUIRE(stats.DrawCalls3D == 0);
    REQUIRE(stats.TrianglesSubmitted == 0);
    REQUIRE(stats.FrustumCulled == 0);

    stats.AddDrawCall(12, false);
    stats.AddDrawCall(3, true);
    stats.AddDrawCall(24, true);
    stats.AddFrustumCulled();
    stats.AddFrustumCulled(4);

    REQUIRE(stats.DrawCalls3D == 3);
    REQUIRE(stats.TrianglesSubmitted == 39);
    REQUIRE(stats.TexturesBound == 2);
    REQUIRE(stats.FrustumCulled == 5);
    REQUIRE(stats.GetTotalVertexCount() == 117);

    stats.Reset();
    REQUIRE(stats.DrawCalls3D == 0);
    REQUIRE(stats.TrianglesSubmitted == 0);
    REQUIRE(stats.TexturesBound == 0);
    REQUIRE(stats.FrustumCulled == 0);
}

// ---------------------------------------------------------------------------
// DecomposeTransform (Hazel's Math)
// ---------------------------------------------------------------------------

TEST_CASE("DecomposeTransform round-trips a composed transform", "[math]")
{
    TransformComponent transform;
    transform.Translation = { 1.0f, 2.0f, 3.0f };
    transform.Rotation = { 0.3f, 0.2f, 0.5f };
    transform.Scale = { 1.0f, 1.0f, 1.0f };

    const glm::mat4 composed = transform.GetTransform();

    glm::vec3 translation{0.0f}, rotation{0.0f}, scale{0.0f};
    REQUIRE(Forge::Math::DecomposeTransform(composed, translation, rotation, scale));

    REQUIRE(translation.x == Approx(1.0f));
    REQUIRE(translation.y == Approx(2.0f));
    REQUIRE(translation.z == Approx(3.0f));
    REQUIRE(scale.x == Approx(1.0f));
    REQUIRE(scale.y == Approx(1.0f));
    REQUIRE(scale.z == Approx(1.0f));

    // The decomposition's Euler convention is glm's; rebuilding with the
    // engine's own composition is the convention-free statement of the
    // round-trip.
    TransformComponent rebuilt;
    rebuilt.Translation = translation;
    rebuilt.Rotation = rotation;
    rebuilt.Scale = scale;

    const glm::mat4 remade = rebuilt.GetTransform();
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            REQUIRE(remade[col][row] == Approx(composed[col][row]).margin(1e-4f));
}

TEST_CASE("DecomposeTransform recovers scale", "[math]")
{
    TransformComponent transform;
    transform.Translation = { 4.0f, -2.0f, 7.0f };
    transform.Rotation = { 0.0f, 0.0f, 0.0f };
    transform.Scale = { 2.0f, 3.0f, 4.0f };

    glm::vec3 translation{0.0f}, rotation{0.0f}, scale{0.0f};
    REQUIRE(Forge::Math::DecomposeTransform(transform.GetTransform(), translation, rotation, scale));

    REQUIRE(translation.x == Approx(4.0f));
    REQUIRE(translation.y == Approx(-2.0f));
    REQUIRE(translation.z == Approx(7.0f));
    REQUIRE(scale.x == Approx(2.0f));
    REQUIRE(scale.y == Approx(3.0f));
    REQUIRE(scale.z == Approx(4.0f));
}

TEST_CASE("DecomposeTransform rejects a degenerate matrix", "[math]")
{
    glm::mat4 degenerate = glm::mat4(1.0f);
    degenerate[3][3] = 0.0f;

    glm::vec3 translation{0.0f}, rotation{0.0f}, scale{0.0f};
    REQUIRE_FALSE(Forge::Math::DecomposeTransform(degenerate, translation, rotation, scale));
}
