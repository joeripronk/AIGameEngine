#include <catch2/catch_test_macros.hpp>

#include "Forge/Renderer/Camera.h"

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
