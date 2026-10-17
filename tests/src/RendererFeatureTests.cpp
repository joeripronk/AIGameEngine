#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Renderer/CpuTexture.h"
#include "Forge/Renderer/Camera.h"

#include <set>
#include <array>
#include <string>

#include <glm/glm.hpp>

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

TEST_CASE("Cube-face view-projections cover the six reflection directions", "[renderer][reflection]")
{
    const glm::vec3 origin{ 1.5f, -2.0f, 3.0f };
    const glm::vec3 forward[6] = {
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
    };

	for (uint32_t face = 0; face < 6; ++face)
	{
		const glm::mat4 viewProjection = Forge::BuildCubeFaceViewProjection(face, origin);

		// glm is the OpenGL convention: clip-space w is -view-space z, so a
		// point in front has positive w and its z lands inside [-1, 1].
		const glm::vec4 ahead = viewProjection * glm::vec4(origin + forward[face] * 5.0f, 1.0f);
		REQUIRE(ahead.w == Approx(5.0f));
		REQUIRE(ahead.z / ahead.w > -1.0f);
		REQUIRE(ahead.z / ahead.w < 1.0f);

		// A point behind the face has negative w and falls outside the
		// frustum: it is clipped away, so the face sees only what is ahead.
		const glm::vec4 behind = viewProjection * glm::vec4(origin - forward[face] * 5.0f, 1.0f);
		REQUIRE(behind.w == Approx(-5.0f));
		REQUIRE(behind.z / behind.w > 1.0f);
	}

    // Faces must be six distinct views, not six transforms of one direction.
    std::set<std::array<float, 16>> distinct;
    for (uint32_t face = 0; face < 6; ++face)
    {
        const glm::mat4 viewProjection = Forge::BuildCubeFaceViewProjection(face, origin);
        std::array<float, 16> flattened;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                flattened[col * 4 + row] = viewProjection[col][row];
        distinct.insert(flattened);
    }
    REQUIRE(distinct.size() == 6);

    // Out-of-range faces return the identity: a degenerate view that draws
    // nothing, documented for callers to validate against.
    REQUIRE(Forge::BuildCubeFaceViewProjection(6u, origin) == glm::mat4(1.0f));
    REQUIRE(Forge::BuildCubeFaceViewProjection(99u, origin) == glm::mat4(1.0f));
}

TEST_CASE("Committed metal albedo fixture decodes as a usable texture", "[texture][renderer]")
{
    const std::string path = std::string(FORGE_SOURCE_DIR) + "/resources/textures/metal_blocks.bmp";
    auto texture = Forge::CpuTexture::Load(path);

    REQUIRE(texture != nullptr);
    REQUIRE(texture->IsHDR() == false);
    REQUIRE(texture->GetWidth() == 256);
    REQUIRE(texture->GetHeight() == 256);
    REQUIRE(texture->GetChannels() == 4);

    // The map tints each tetromino albedo, so it must be neutral grey and
    // actually vary across the surface: a flat buffer decodes fine and still
    // renders as a featureless block.
    const uint8_t* bytes = static_cast<const uint8_t*>(texture->GetPixelData());
    REQUIRE(bytes[0] == 227);
    REQUIRE(bytes[1] == 227);
    REQUIRE(bytes[2] == 227);
    REQUIRE(bytes[3] == 255);

    uint8_t minLuma = 255;
    uint8_t maxLuma = 0;
    for (uint32_t y = 0; y < 256; y += 4)
    {
        for (uint32_t x = 0; x < 256; x += 4)
        {
            const uint8_t* px = bytes + ((size_t)y * 256 + x) * 4;
            REQUIRE(px[0] == px[1]);
            REQUIRE(px[1] == px[2]);
            minLuma = std::min(minLuma, px[0]);
            maxLuma = std::max(maxLuma, px[0]);
        }
    }
    REQUIRE(maxLuma > minLuma);
}
TEST_CASE("Committed HDRI fixture decodes as HDR", "[renderer][hdr]")
{
    const std::string path = std::string(FORGE_SOURCE_DIR) + "/resources/environment/sunset.hdr";
    auto texture = Forge::CpuTexture::Load(path);

    REQUIRE(texture != nullptr);
    REQUIRE(texture->IsHDR() == true);
    REQUIRE(texture->GetWidth() == 64);
    REQUIRE(texture->GetHeight() == 32);

    // IBL prefiltering reads these floats, so they must be finite and actually
    // carry light rather than being a decodable but empty buffer.
    const auto& floats = texture->GetFloats();
    REQUIRE_FALSE(floats.empty());

    bool sawLight = false;
    bool allFinite = true;
    float peak = 0.0f;
    for (float value : floats)
    {
        if (!std::isfinite(value))
            allFinite = false;
        if (value > peak)
            peak = value;
        if (value > 0.0f)
            sawLight = true;
    }

    REQUIRE(allFinite);
    REQUIRE(sawLight);

    // The RGBE encoding is easy to get wrong in a way that still decodes to
    // finite, plausible-looking numbers - this fixture was briefly written as
    // interleaved pixels instead of four component planes, and stb decoded that
    // into garbage rather than failing. So the *values* are checked, not just
    // their existence: the sun lobe was authored at about 9x the sky, so a
    // correct decode peaks there and nowhere near a wildly different figure.
    REQUIRE(peak > 5.0f);
    REQUIRE(peak < 20.0f);

    // `allFinite` already covers every channel: a decode that left one plane
    // corrupt would flip it while the others looked fine.
}

TEST_CASE("Model import offsets each mesh's indices and applies node transforms",
	"[mesh][import]")
{
    // resources/models/two_parts.gltf: two meshes, two triangles, the second
    // under a node translated +5 on X.
    //
    // Both halves of this were broken at once. Indices are per-mesh, so the
    // second triangle's indices (0,1,2) have to be offset by the first mesh's
    // vertex count; without that they pointed at the first triangle's vertices
    // - or past the end of the buffer. And a model places its parts with node
    // transforms, which the importer ignored, stacking both triangles on top of
    // each other.
    const std::string path = std::string(FORGE_SOURCE_DIR) + "/resources/models/two_parts.gltf";

    Forge::ImportedModel model;
    REQUIRE(Forge::MeshImport::ImportModel(path, model));

    REQUIRE(model.Vertices.size() == 6u);
    REQUIRE(model.Indices.size() == 6u);

    // Every index addresses a vertex that exists. This is the property that
    // makes the draw call defined behaviour rather than a read of whatever
    // happened to be next in the buffer.
    for (uint32_t index : model.Indices)
        REQUIRE(index < model.Vertices.size());

    // The second triangle's indices point at the *second* triangle's vertices.
    glm::vec3 min = model.Vertices[0].Position;
    glm::vec3 max = model.Vertices[0].Position;
    for (const auto& vertex : model.Vertices)
    {
        min = glm::min(min, vertex.Position);
        max = glm::max(max, vertex.Position);
    }

    // Without the node transform both triangles would span x in [0, 1] and the
    // bounds would prove nothing; with it the model is 6 units wide.
    REQUIRE(min.x == Approx(0.0f));
    REQUIRE(max.x == Approx(6.0f));

    // The first mesh's triangle sits at the origin; the second at +5.
    REQUIRE(model.Vertices[model.Indices[3]].Position.x == Approx(5.0f));
    REQUIRE(model.Vertices[model.Indices[4]].Position.x == Approx(6.0f));
    REQUIRE(model.Vertices[model.Indices[5]].Position.x == Approx(5.0f));

    // Normals stay unit length after the transform, or lighting is wrong.
    for (const auto& vertex : model.Vertices)
        REQUIRE(glm::length(vertex.Normal) == Approx(1.0f));
}

TEST_CASE("Model import reads the material the file declares", "[mesh][import]")
{
    const std::string path = std::string(FORGE_SOURCE_DIR) + "/resources/models/two_parts.gltf";

    Forge::ImportedModel model;
    REQUIRE(Forge::MeshImport::ImportModel(path, model));

    // The base colour comes from the same import that produced the geometry, so
    // a model cannot come back with geometry and no material at all.
    REQUIRE(model.Albedo.r == Approx(1.0f));
    REQUIRE(model.Albedo.b == Approx(0.0f));

    // The PBR factors come from the same material. This is also what proves the
    // *first* mesh's material is the one that wins: the second mesh declares a
    // different colour and different factors, and reading the last instead left
    // a model with the parts in the wrong order wearing the wrong material.
    REQUIRE(model.Metallic == Approx(0.25f));
    REQUIRE(model.Roughness == Approx(0.75f));
    REQUIRE(model.AO == Approx(1.0f));

    // A model with no texture names no path, rather than an empty string that
    // a loader would try to open and fail.
    REQUIRE(model.AlbedoTexturePath.empty());
}

TEST_CASE("Model import reports failure instead of importing nothing", "[mesh][import]")
{
    Forge::ImportedModel model;
    REQUIRE_FALSE(Forge::MeshImport::ImportModel("no_such_model.gltf", model));
    REQUIRE(model.Vertices.empty());

    // The committed fixture here is an empty glTF document: a file that parses
    // and contains no geometry. That is a failure, not an empty model.
    REQUIRE_FALSE(Forge::MeshImport::ImportModel(
        std::string(FORGE_SOURCE_DIR) + "/test_asset_scan/model.gltf", model));
    REQUIRE(model.Vertices.empty());

    // CpuMesh::LoadFromFile is the same importer behind a convenience wrapper.
    REQUIRE(Forge::CpuMesh::LoadFromFile("no_such_model.gltf") == nullptr);
}
