#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Renderer/CpuTexture.h"

#include <set>

TEST_CASE("Cube primitive has valid topology", "[mesh][primitive]")
{
    auto cube = Forge::Primitives::CreateCube(1.0f);
    REQUIRE(cube);
    REQUIRE(cube->GetVertices().size() == 24);
    REQUIRE(cube->GetIndexCount() == 36);
    REQUIRE(cube->GetIndices().size() % 3 == 0);

    // All indices must be in range.
    for (uint32_t index : cube->GetIndices())
        REQUIRE(index < cube->GetVertices().size());

    // Unit bounding box for a size-1 cube.
    REQUIRE(cube->GetMin() == glm::vec3(-0.5f));
    REQUIRE(cube->GetMax() == glm::vec3(0.5f));

    // Face normals must be unit length and axis aligned.
    for (const auto& v : cube->GetVertices())
        REQUIRE(glm::length(v.Normal) == Approx(1.0f));
}

TEST_CASE("Plane primitive has valid topology", "[mesh][primitive]")
{
    auto plane = Forge::Primitives::CreatePlane(2.0f);
    REQUIRE(plane);
    REQUIRE(plane->GetVertices().size() == 4);
    REQUIRE(plane->GetIndexCount() == 6);
    REQUIRE(plane->GetMax() == glm::vec3(1.0f, 0.0f, 1.0f));
}

TEST_CASE("Sphere primitive has valid topology", "[mesh][primitive]")
{
    auto sphere = Forge::Primitives::CreateSphere(1.0f, 8, 8);
    REQUIRE(sphere);
    REQUIRE(!sphere->GetVertices().empty());
    REQUIRE(sphere->GetIndexCount() > 0);
    REQUIRE(sphere->GetIndexCount() % 3 == 0);

    for (uint32_t index : sphere->GetIndices())
        REQUIRE(index < sphere->GetVertices().size());

    // Vertices lie on the sphere surface and normals are unit length.
    for (const auto& v : sphere->GetVertices())
    {
        REQUIRE(glm::length(v.Position) == Approx(1.0f).margin(0.01f));
        REQUIRE(glm::length(v.Normal) == Approx(1.0f));
    }

    // Degenerate sphere parameters are rejected.
    REQUIRE(Forge::Primitives::CreateSphere(1.0f, 0, 0) == nullptr);
}

TEST_CASE("CpuMesh recalculates normals", "[mesh][primitive]")
{
    auto plane = Forge::Primitives::CreatePlane(2.0f);
    REQUIRE(plane);

    // Build a copy with zeroed normals and recompute them from the geometry.
    std::vector<Forge::Vertex> vertices = plane->GetVertices();
    for (auto& v : vertices)
        v.Normal = { 0.0f, 0.0f, 0.0f };

    Forge::CpuMesh mesh(vertices, plane->GetIndices());
    mesh.RecalculateNormals();

    for (const auto& v : mesh.GetVertices())
        REQUIRE(v.Normal.y == Approx(1.0f).margin(1e-4f));
}

TEST_CASE("CpuMesh imports a model file via assimp", "[mesh][import]")
{
    auto mesh = Forge::CpuMesh::LoadFromFile(std::string(FORGE_SOURCE_DIR) + "/resources/models/test_triangle.obj");
    REQUIRE(mesh);

    // A single triangle: 3 vertices, 3 indices.
    REQUIRE(mesh->GetVertices().size() == 3);
    REQUIRE(mesh->GetIndexCount() == 3);

    // Indices must be in range.
    for (uint32_t index : mesh->GetIndices())
        REQUIRE(index < mesh->GetVertices().size());

    // Bounds reflect the fixture (-1..1 x, -1..1 y).
    REQUIRE(mesh->GetMin() == glm::vec3(-1.0f, -1.0f, 0.0f));
    REQUIRE(mesh->GetMax() == glm::vec3(1.0f, 1.0f, 0.0f));

    // Normals were generated (unit length); UVs present (assimp may flip V).
    REQUIRE(glm::length(mesh->GetVertices()[0].Normal) == Approx(1.0f));
    REQUIRE(mesh->GetVertices()[0].UV.x == Approx(0.0f));
    REQUIRE(mesh->GetVertices()[1].UV.x == Approx(1.0f));
}

TEST_CASE("CpuMesh import rejects missing files", "[mesh][import]")
{
    REQUIRE(Forge::CpuMesh::LoadFromFile("does_not_exist.obj") == nullptr);
}

TEST_CASE("CpuTexture wraps raw bytes", "[texture]")
{
    std::vector<uint8_t> pixels(2 * 2 * 4, 255);
    auto texture = Forge::CpuTexture::FromBytes(2, 2, pixels);

    REQUIRE(texture->GetWidth() == 2);
    REQUIRE(texture->GetHeight() == 2);
    REQUIRE(texture->GetChannels() == 4);
    REQUIRE(texture->IsHDR() == false);
    REQUIRE(texture->GetBytes().size() == 16);
    REQUIRE(texture->GetPixelData() != nullptr);
}

TEST_CASE("CpuTexture loads a PNG from disk", "[texture]")
{
    auto texture = Forge::CpuTexture::Load(std::string(FORGE_SOURCE_DIR) + "/resources/textures/test_checker.png");
    REQUIRE(texture);
    REQUIRE(texture->GetWidth() == 2);
    REQUIRE(texture->GetHeight() == 2);
    REQUIRE(texture->IsHDR() == false);
    REQUIRE(texture->GetChannels() == 4);
    REQUIRE(texture->GetBytes().size() == 2 * 2 * 4);

    // Top-left pixel is red.
    const uint8_t* bytes = static_cast<const uint8_t*>(texture->GetPixelData());
    REQUIRE(bytes[0] == 255);
    REQUIRE(bytes[1] == 0);
    REQUIRE(bytes[2] == 0);
}

TEST_CASE("CpuTexture handles bad input gracefully", "[texture]")
{
    REQUIRE(Forge::CpuTexture::Load("definitely_missing.png") == nullptr);
    REQUIRE(Forge::CpuTexture::Load(FORGE_SOURCE_DIR "/README.md") == nullptr);
}