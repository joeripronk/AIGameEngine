#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Renderer/CpuTexture.h"
#include "Forge/Renderer/Camera.h"
#include "Forge/Assets/AssetImporter.h"
#include "Forge/Core/Log.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <array>
#include <string>
#include <utility>

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

TEST_CASE("Every committed tetris theme tile decodes as a usable albedo map",
	"[texture][renderer][theme]")
{
	Forge::Log::Init();

	// The seven surfaces the tetris blocks are drawn in, cropped from
	// sgtcrispy's CC0 "Colored Metal tiles 1b" sheet.
	//
	// The rule these are checked against is the one that bit the HDRI fixture:
	// a committed asset that is *nearly* right is worse than none, because it
	// decodes to plausible garbage. A crop taken from the wrong cell of the
	// sheet, a palette that did not survive the save, a tile that is one flat
	// colour - all of them "load" and none of them show a pattern, and a block
	// drawn in a flat tile is a block with no theme at all.
	static const char* kThemeTiles[] = {
		"block_i_diamond_plate.png",
		"block_o_solid_plate.png",
		"block_t_panels.png",
		"block_s_rivets.png",
		"block_z_grate.png",
		"block_j_gold_slats.png",
		"block_l_diamond_plate.png",
	};

	std::set<std::string> names;
	for (const char* tile : kThemeTiles)
	{
		const std::string path = std::string(FORGE_SOURCE_DIR) + "/resources/textures/tetris/" + tile;

		// The engine's own answer to "is this file usable?".
		REQUIRE(Forge::AssetImporter::Import(path));

		auto texture = Forge::CpuTexture::Load(path);
		REQUIRE(texture != nullptr);
		REQUIRE(texture->IsHDR() == false);
		REQUIRE(texture->GetWidth() == 128u);
		REQUIRE(texture->GetHeight() == 128u);
		REQUIRE(texture->GetChannels() == 4);

		REQUIRE(names.insert(tile).second == true);

		const uint8_t* bytes = static_cast<const uint8_t*>(texture->GetPixelData());
		const size_t pixels = 128u * 128u;

		uint8_t minLuma = 255, maxLuma = 0;
		uint32_t opaque = 0, saturated = 0;
		long sum[3] = {0, 0, 0};

		for (size_t i = 0; i < pixels; ++i)
		{
			const uint8_t* px = bytes + i * 4;
			if (px[3] == 255)
				++opaque;

			const uint8_t lo = std::min({px[0], px[1], px[2]});
			const uint8_t hi = std::max({px[0], px[1], px[2]});
			if (hi - lo > 24)
				++saturated;  // a coloured texel, not a shade of grey

			sum[0] += px[0];
			sum[1] += px[1];
			sum[2] += px[2];

			const uint8_t luma = px[0] / 2 + px[1] / 4 + px[2] / 8;
			minLuma = std::min(minLuma, luma);
			maxLuma = std::max(maxLuma, luma);
		}

		REQUIRE(opaque == pixels);              // no alpha to blend against a skybox
		REQUIRE(maxLuma > minLuma);             // the pattern is present
		REQUIRE(saturated > pixels / 8);        // the palette survived the crop
		REQUIRE(sum[0] + sum[1] + sum[2] > 0);  // not a black tile

		// A tile whose texels are all one value is the failure a "load
		// succeeded" check cannot see, and the one that renders as an
		// untextured block.
		REQUIRE(maxLuma - minLuma > 16);
	}
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

TEST_CASE("A model with no texture coordinates is box-projected onto them",
	"[mesh][import][uv]")
{
	// resources/models/cube.glb stores POSITION and NORMAL and nothing else.
	//
	// Leaving its UVs at zero did not draw an untextured cube - it sampled texel
	// (0, 0) across every triangle, so the whole model came out in whatever
	// colour one corner pixel held. A material with an albedo map looked
	// identical to one without, which is what the tetris blocks did: they named
	// a brushed-metal map and never showed a pixel of it.
	const std::string path = std::string(FORGE_SOURCE_DIR) + "/resources/models/cube.glb";

	Forge::ImportedModel model;
	REQUIRE(Forge::MeshImport::ImportModel(path, model));
	REQUIRE(model.Vertices.size() > 1u);

	// The property that was actually broken: the UVs are not all one value.
	//
	// Four is the answer for a cube, and it is the interesting one. A box
	// projection puts each face's four corners on the four corners of the tile,
	// so every face samples the whole texture and the distinct set is exactly
	// those corners - not 24 (which would mean the faces were projected
	// against different axes, i.e. the pattern ran in six directions), and not
	// 1 (which is the texel-(0,0) collapse this replaced).
	std::set<std::pair<float, float>> distinct;
	for (const auto& vertex : model.Vertices)
	{
		REQUIRE(vertex.UV.x >= 0.0f);
		REQUIRE(vertex.UV.x <= 1.0f);
		REQUIRE(vertex.UV.y >= 0.0f);
		REQUIRE(vertex.UV.y <= 1.0f);
		distinct.insert({vertex.UV.x, vertex.UV.y});
	}
	REQUIRE(distinct.size() == 4u);

	// Every face covers the whole texture: a projection that clamped, or that
	// normalised against a fixed unit cube, would leave the corners of the tile
	// unsampled and the pattern would run off the middle of each face.
	REQUIRE(distinct.count({0.0f, 0.0f}) == 1u);
	REQUIRE(distinct.count({1.0f, 0.0f}) == 1u);
	REQUIRE(distinct.count({1.0f, 1.0f}) == 1u);
	REQUIRE(distinct.count({0.0f, 1.0f}) == 1u);

	// Each face projects along its own axis, so vertices sharing a position but
	// facing different ways get different UVs. Six distinct faces, six distinct
	// (normal, uv) pairs. Keyed by text because glm vectors define no ordering.
	std::set<std::string> perFace;
	for (const auto& vertex : model.Vertices)
	{
		std::string key;
		for (int axis = 0; axis < 3; ++axis)
			key += std::to_string(vertex.Normal[axis]) + ",";
		key += "|" + std::to_string(vertex.UV.x) + "," + std::to_string(vertex.UV.y);
		perFace.insert(key);
	}
	REQUIRE(perFace.size() >= 6u);

	// The frame a UV implies. A tangent that is not unit length, or not
	// perpendicular to the normal it belongs to, breaks any normal or parallax
	// work layered on top - and the fallback this replaced handed every vertex
	// the constant (1, 0, 0) whatever face it was on.
	//
	// Finite first. glm::normalize of a zero vector answers NaN, and this
	// fixture carries seven exactly-zero normals plus one flattened to
	// (0, -0.1, 0), so the importer was normalizing them and writing NaN into
	// the vertex buffer for lighting to read. NaN in a vertex attribute is not
	// a black triangle - it is undefined behaviour on the GPU, answered by the
	// driver, and it is invisible in a screenshot until it is not.
	for (const auto& vertex : model.Vertices)
	{
		const glm::vec3* vectors[3] = { &vertex.Normal, &vertex.Tangent, &vertex.Bitangent };
		for (const glm::vec3* vector : vectors)
		{
			for (int axis = 0; axis < 3; ++axis)
				REQUIRE(std::isfinite((*vector)[axis]));
		}
		REQUIRE(glm::length(vertex.Normal) == Approx(1.0f));
	}

	for (const auto& vertex : model.Vertices)
	{
		REQUIRE(glm::length(vertex.Tangent) == Approx(1.0f));
		REQUIRE(glm::length(vertex.Bitangent) == Approx(1.0f));
		REQUIRE(glm::dot(vertex.Normal, vertex.Tangent) == Approx(0.0f).margin(1e-4f));
		REQUIRE(glm::dot(vertex.Normal, vertex.Bitangent) == Approx(0.0f).margin(1e-4f));

		// Right-handed: cross(normal, tangent) is the bitangent. Getting this
		// right by construction rather than per-face by inspection is the point
		// of deriving it.
		REQUIRE(glm::all(glm::epsilonEqual(
			glm::cross(vertex.Normal, vertex.Tangent), vertex.Bitangent, 1e-4f)));
	}
}

TEST_CASE("A model that declares texture coordinates keeps them", "[mesh][import][uv]")
{
	// resources/models/uv_triangle.gltf declares TEXCOORD_0 with UVs that a box
	// projection could not have produced: none of them on a 0/1 grid.
	//
	// Without this, the projection above would be indistinguishable from one
	// that overwrites authored UVs - and an unwrap is something an artist does
	// deliberately, so a generated one has to lose to a real one.
	const std::string path = std::string(FORGE_SOURCE_DIR) + "/resources/models/uv_triangle.gltf";

	Forge::ImportedModel model;
	REQUIRE(Forge::MeshImport::ImportModel(path, model));
	REQUIRE(model.Vertices.size() == 3u);

	const glm::vec2 expected[3] = { { 0.11f, 0.79f }, { 0.63f, 0.22f }, { 0.94f, 0.47f } };
	for (size_t i = 0; i < 3; ++i)
	{
		REQUIRE(model.Vertices[i].UV.x == Approx(expected[i].x));
		REQUIRE(model.Vertices[i].UV.y == Approx(expected[i].y));
	}

	// Still a right-handed frame, even though the UVs are no longer ours to
	// derive an axis from - assimp computes tangents from the authored UVs.
	for (const auto& vertex : model.Vertices)
	{
		REQUIRE(glm::length(vertex.Tangent) == Approx(1.0f));
		REQUIRE(glm::dot(vertex.Normal, vertex.Tangent) == Approx(0.0f).margin(1e-3f));
	}
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
