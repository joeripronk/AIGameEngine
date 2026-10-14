#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Renderer/Camera.h"
#include "Forge/Renderer/EditorCamera.h"
#include "Forge/Renderer/RenderCommand.h"

TEST_CASE("Camera default construction", "[renderer][camera]")
{
    Forge::Camera camera;

    REQUIRE(camera.GetPosition() == glm::vec3(0.0f, 0.0f, 5.0f));
    REQUIRE(camera.GetRotation() == glm::vec3(0.0f, 0.0f, 0.0f));
}

TEST_CASE("Camera set position", "[renderer][camera]")
{
    Forge::Camera camera;
    camera.SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));

    REQUIRE(camera.GetPosition() == glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("Camera set rotation", "[renderer][camera]")
{
    Forge::Camera camera;
    camera.SetRotation(glm::vec3(90.0f, 45.0f, 0.0f));

    REQUIRE(camera.GetRotation() == glm::vec3(90.0f, 45.0f, 0.0f));
}

TEST_CASE("Camera view projection matrix is valid", "[renderer][camera]")
{
    Forge::Camera camera;
    camera.SetProjection(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    camera.SetPosition(glm::vec3(0.0f, 0.0f, 10.0f));
    camera.SetRotation(glm::vec3(0.0f, 0.0f, 0.0f));

    const auto& vp = camera.GetViewProjectionMatrix();
    REQUIRE(vp[0][0] != 0.0f);
    REQUIRE(vp[1][1] != 0.0f);
}

TEST_CASE("Camera projection matrix is perspective", "[renderer][camera]")
{
    Forge::Camera camera;
    camera.SetProjection(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

    const auto& proj = camera.GetProjectionMatrix();
    // Perspective projection should have a non-zero w component
    REQUIRE(proj[3][2] != 0.0f);
}

TEST_CASE("Camera view matrix is inverse of transform", "[renderer][camera]")
{
    Forge::Camera camera;
    camera.SetPosition(glm::vec3(0.0f, 0.0f, 10.0f));
    camera.SetRotation(glm::vec3(0.0f, 0.0f, 0.0f));

    const auto& view = camera.GetViewMatrix();
    // Camera at (0,0,10) looking at origin: view matrix translates by (0,0,-10)
    REQUIRE(view[3][2] == Approx(-10.0f));
}

TEST_CASE("EditorCamera default construction", "[renderer][editorcamera]")
{
    Forge::EditorCamera camera;

    REQUIRE(camera.GetCameraPosition() == glm::vec3(0.0f, 4.0f, 10.0f));
    REQUIRE(camera.GetCameraRotation() == glm::vec3(0.0f, 0.0f, 0.0f));
    REQUIRE(camera.GetZoomLevel() == Approx(1.0f));
}

TEST_CASE("EditorCamera set position", "[renderer][editorcamera]")
{
    Forge::EditorCamera camera;
    camera.SetPosition(glm::vec3(1.0f, 2.0f, 3.0f));

    REQUIRE(camera.GetCameraPosition() == glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("EditorCamera set rotation", "[renderer][editorcamera]")
{
    Forge::EditorCamera camera;
    camera.SetRotation(glm::vec3(45.0f, 90.0f, 0.0f));

    REQUIRE(camera.GetCameraRotation() == glm::vec3(45.0f, 90.0f, 0.0f));
}

TEST_CASE("EditorCamera projection matrix is valid", "[renderer][editorcamera]")
{
    Forge::EditorCamera camera;
    camera.SetProjection(60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);

    const auto& vp = camera.GetViewProjectionMatrix();
    REQUIRE(vp[0][0] != 0.0f);
    REQUIRE(vp[1][1] != 0.0f);
}

TEST_CASE("EditorCamera zoom level", "[renderer][editorcamera]")
{
    Forge::EditorCamera camera;
    camera.SetZoomLevel(2.0f);

    REQUIRE(camera.GetZoomLevel() == Approx(2.0f));
}
