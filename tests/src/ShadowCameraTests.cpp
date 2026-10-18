#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Renderer/ShadowCamera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

using namespace Forge;

namespace {

	std::vector<std::pair<glm::vec3, glm::vec3>> OneBox(const glm::vec3& min, const glm::vec3& max)
	{
		return { { min, max } };
	}

	// Every corner of every box, measured through the camera the way the shader
	// measures a fragment: divided by w into NDC.
	std::vector<glm::vec3> CornersThrough(const ShadowCamera& camera,
		const std::vector<std::pair<glm::vec3, glm::vec3>>& boxes)
	{
		std::vector<glm::vec3> ndc;
		for (const auto& box : boxes)
		{
			for (int i = 0; i < 8; ++i)
			{
				const glm::vec3 corner(
					(i & 1) ? box.second.x : box.first.x,
					(i & 2) ? box.second.y : box.first.y,
					(i & 4) ? box.second.z : box.first.z);

				const glm::vec4 clip = camera.ViewProjection * glm::vec4(corner, 1.0f);
				ndc.push_back(glm::vec3(clip) / clip.w);
			}
		}
		return ndc;
	}

	// What pbr.frag's SampleShadow does with a corner: it maps NDC into texel
	// space with `p * 0.5 + 0.5` and declines to shade anything outside the map,
	// so "inside" is the NDC cube [-1,1] on every axis. A scene that lands
	// outside is a scene whose shading is decided by a rule that has nothing to
	// do with the geometry.
	bool InsideMap(const glm::vec3& ndc)
	{
		return ndc.x >= -1.0f && ndc.x <= 1.0f && ndc.y >= -1.0f && ndc.y <= 1.0f &&
			ndc.z >= -1.0f && ndc.z <= 1.0f;
	}

}

// ---------------------------------------------------------------------------
// Framing: the map has to cover the scene
// ---------------------------------------------------------------------------

TEST_CASE("Shadow camera frames the scene inside the map", "[renderer][shadow]")
{
	// A tetris-sized field: ten cells wide, twenty tall, lit from the camera
	// side. A fixed +-10 ortho box cannot hold this, and everything outside it
	// is unshadowed by the shader's own early return.
	const std::vector<std::pair<glm::vec3, glm::vec3>> boxes = {
		{ { -5.0f, -10.0f, -0.5f }, { 5.0f, 10.0f, 0.5f } },
		{ { -6.0f, -11.0f, -4.5f }, { 6.0f, 11.0f, -4.0f } },
	};

	const ShadowCamera camera = BuildShadowCamera(boxes, { 0.2f, 0.2f, 0.95f });
	REQUIRE(camera.Valid);

	for (const glm::vec3& ndc : CornersThrough(camera, boxes))
		REQUIRE(InsideMap(ndc));
}

TEST_CASE("Shadow camera covers a scene in every direction the light can come from", "[renderer][shadow]")
{
	const std::vector<std::pair<glm::vec3, glm::vec3>> boxes = {
		{ { -3.0f, -2.0f, -1.0f }, { 3.0f, 4.0f, 1.0f } },
	};

	// The up axis has to follow the light: lookAt is undefined when up is
	// parallel to the view direction, and a light straight down is the common
	// case in a game.
	const glm::vec3 directions[] = {
		{ 0.0f, 1.0f, 0.0f },   // straight up
		{ 0.0f, -1.0f, 0.0f },  // straight down
		{ 1.0f, 0.0f, 0.0f },
		{ -1.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f },
		{ 0.0f, 0.0f, -1.0f },
		{ 0.45f, 0.45f, 0.75f },
		{ -0.5f, -1.0f, -0.3f },
	};

	for (const glm::vec3& direction : directions)
	{
		const ShadowCamera camera = BuildShadowCamera(boxes, direction);
		INFO("direction " << direction.x << ", " << direction.y << ", " << direction.z);
		REQUIRE(camera.Valid);

		for (const glm::vec3& ndc : CornersThrough(camera, boxes))
			REQUIRE(InsideMap(ndc));
	}
}

TEST_CASE("Shadow camera covers a rotated scene, not just its axis-aligned box", "[renderer][shadow]")
{
	// The corners of a box rotated 45 degrees about the light's axis stick out
	// past the world AABB. Framing on the world AABB leaves them outside.
	const glm::mat4 rotation = glm::mat4(glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f)));
	const glm::vec3 min(-2.0f, -1.0f, -1.0f);
	const glm::vec3 max(2.0f, 1.0f, 1.0f);

	glm::vec3 rotatedMin(std::numeric_limits<float>::max());
	glm::vec3 rotatedMax(std::numeric_limits<float>::lowest());
	std::vector<std::pair<glm::vec3, glm::vec3>> boxes;
	for (int i = 0; i < 8; ++i)
	{
		const glm::vec3 corner(
			(i & 1) ? max.x : min.x,
			(i & 2) ? max.y : min.y,
			(i & 4) ? max.z : min.z);
		const glm::vec3 rotated = glm::mat3(rotation) * corner;
		rotatedMin = glm::min(rotatedMin, rotated);
		rotatedMax = glm::max(rotatedMax, rotated);
	}
	boxes = OneBox(rotatedMin, rotatedMax);

	const ShadowCamera camera = BuildShadowCamera(boxes, glm::normalize(glm::vec3(0.3f, 0.4f, 0.86f)));
	REQUIRE(camera.Valid);
	for (const glm::vec3& ndc : CornersThrough(camera, boxes))
		REQUIRE(InsideMap(ndc));
}

// ---------------------------------------------------------------------------
// Depth: everything is in front of the near plane, behind the far plane
// ---------------------------------------------------------------------------

TEST_CASE("Shadow camera puts the whole scene between its near and far planes", "[renderer][shadow]")
{
	const std::vector<std::pair<glm::vec3, glm::vec3>> boxes = {
		{ { -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f } },
		{ { 8.0f, 0.0f, 0.0f }, { 9.0f, 1.0f, 1.0f } },
	};

	const ShadowCamera camera = BuildShadowCamera(boxes, { 1.0f, 0.3f, 0.2f });
	REQUIRE(camera.Valid);
	REQUIRE(camera.NearPlane > 0.0f);
	REQUIRE(camera.FarPlane > camera.NearPlane);
	REQUIRE_FALSE(std::isinf(camera.FarPlane));

	for (const glm::vec3& ndc : CornersThrough(camera, boxes))
	{
		REQUIRE(ndc.z > -1.0f);
		REQUIRE(ndc.z < 1.0f);
	}
}

TEST_CASE("Shadow camera reads depth in the direction the light lights", "[renderer][shadow]")
{
	// Two boxes along the light: the nearer one must measure shallower in light
	// space, which is what makes a shadow fall on the far one rather than on
	// itself.
	const glm::vec3 toLight = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f));
	const std::vector<std::pair<glm::vec3, glm::vec3>> near = OneBox({ 0.0f, 0.0f, 4.0f }, { 1.0f, 1.0f, 5.0f });
	const std::vector<std::pair<glm::vec3, glm::vec3>> far = OneBox({ 0.0f, 0.0f, -5.0f }, { 1.0f, 1.0f, -4.0f });

	const ShadowCamera camera = BuildShadowCamera({ near.front(), far.front() }, toLight);
	REQUIRE(camera.Valid);

	const std::vector<glm::vec3> nearCorners = CornersThrough(camera, near);
	const std::vector<glm::vec3> farCorners = CornersThrough(camera, far);

	float deepestNear = 0.0f;
	for (const glm::vec3& ndc : nearCorners)
		deepestNear = std::max(deepestNear, ndc.z);
	float shallowestFar = 1.0f;
	for (const glm::vec3& ndc : farCorners)
		shallowestFar = std::min(shallowestFar, ndc.z);

	REQUIRE(deepestNear < shallowestFar);
}

// ---------------------------------------------------------------------------
// Degenerate input answers "no camera", not a transform
// ---------------------------------------------------------------------------

TEST_CASE("Shadow camera reports no camera for input it cannot frame", "[renderer][shadow]")
{
	// Identity is not "no shadows": an identity view-projection projects
	// geometry by its own world position, so the map holds a picture of the
	// scene from nowhere and shading lands at arbitrary places in it. An
	// invalid camera has to be recognisable as such.
	const ShadowCamera noGeometry = BuildShadowCamera({}, { 0.0f, 1.0f, 0.0f });
	REQUIRE_FALSE(noGeometry.Valid);
	REQUIRE(noGeometry.ViewProjection == glm::mat4(1.0f));

	const ShadowCamera noDirection = BuildShadowCamera(OneBox({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }),
		glm::vec3(0.0f));
	REQUIRE_FALSE(noDirection.Valid);

	const ShadowCamera nanDirection = BuildShadowCamera(OneBox({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f }),
		glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));
	REQUIRE_FALSE(nanDirection.Valid);
}

TEST_CASE("Shadow camera frames a flat scene without a zero-width volume", "[renderer][shadow]")
{
	// A floor is a box with no height. An ortho volume with a zero span on one
	// axis has no inverse, so a flat scene would answer "no camera" for every
	// fragment - which is the same as no shadows, arrived at by a route that
	// looks like success.
	const ShadowCamera flat = BuildShadowCamera(OneBox({ -10.0f, 0.0f, -10.0f }, { 10.0f, 0.0f, 10.0f }),
		{ 0.2f, 0.9f, 0.35f });
	REQUIRE(flat.Valid);
	REQUIRE(flat.FarPlane > flat.NearPlane);
	REQUIRE(flat.NearPlane > 0.0f);

	const std::vector<glm::vec3> corners = CornersThrough(flat, OneBox({ -10.0f, 0.0f, -10.0f }, { 10.0f, 0.0f, 10.0f }));
	for (const glm::vec3& ndc : corners)
		REQUIRE(InsideMap(ndc));
}

TEST_CASE("Shadow camera is the same answer every frame for the same scene", "[renderer][shadow]"){
	// A map that jitters between frames shimmers; a map that depends on the
	// camera's position is not the light's map.
	const std::vector<std::pair<glm::vec3, glm::vec3>> boxes = {
		{ { -2.0f, -3.0f, -1.0f }, { 2.0f, 3.0f, 1.0f } },
	};
	const glm::vec3 direction(0.4f, 0.5f, 0.77f);

	const ShadowCamera first = BuildShadowCamera(boxes, direction);
	const ShadowCamera second = BuildShadowCamera(boxes, direction * 2.0f);

	REQUIRE(first.Valid);
	REQUIRE(second.Valid);
	for (int column = 0; column < 4; ++column)
		for (int row = 0; row < 4; ++row)
			REQUIRE(first.ViewProjection[column][row] == Approx(second.ViewProjection[column][row]));
}

// ---------------------------------------------------------------------------
// Bias: a number of texels, not a number out of the air
// ---------------------------------------------------------------------------

TEST_CASE("Shadow bias scales with the texel it has to clear", "[renderer][shadow]")
{
	// The offset exists to move a sample off the surface it is sampling
	// against, so it is measured in texels: a map whose texels are ten times
	// coarser needs ten times the reach, and a bias that does not scale leaves
	// the coarse map acne-covered while the fine one peter-pans.
	const ShadowBias fine = ComputeShadowBias(0.01f, 0.05f);
	const ShadowBias coarse = ComputeShadowBias(0.10f, 0.05f);

	REQUIRE(fine.NormalOffsetWorld > 0.0f);
	REQUIRE(coarse.NormalOffsetWorld == Approx(fine.NormalOffsetWorld * 10.0f));
	REQUIRE(coarse.DepthBiasNdc > fine.DepthBiasNdc);
}

TEST_CASE("Shadow bias answers zero when there is no map to bias against", "[renderer][shadow]")
{
	// A bias for a map that does not exist is a number describing a volume
	// nobody measured - and a positive one makes the shader lift every fragment
	// off every surface, which is "no shadows" wearing the look of success.
	REQUIRE(ComputeShadowBias(0.0f, 0.05f).NormalOffsetWorld == 0.0f);
	REQUIRE(ComputeShadowBias(0.0f, 0.05f).DepthBiasNdc == 0.0f);
	REQUIRE(ComputeShadowBias(0.01f, 0.0f).DepthBiasNdc == 0.0f);
	REQUIRE(ComputeShadowBias(-0.01f, 0.05f).DepthBiasNdc == 0.0f);
	REQUIRE(ComputeShadowBias(std::numeric_limits<float>::quiet_NaN(), 0.05f).NormalOffsetWorld == 0.0f);
	REQUIRE(ComputeShadowBias(0.01f, std::numeric_limits<float>::infinity()).DepthBiasNdc == 0.0f);
}

TEST_CASE("Shadow bias stays in the range the depth test can use", "[renderer][shadow]")
{
	// The bias is added to a fragment's depth before it is compared against the
	// far plane. A bias of a whole NDC unit is "shade nothing, ever".
	const ShadowBias bias = ComputeShadowBias(0.01f, 2.0f / 12.0f);
	REQUIRE(bias.DepthBiasNdc > 0.0f);
	REQUIRE(bias.DepthBiasNdc < 0.1f);
	REQUIRE(bias.NormalOffsetWorld < 1.0f);
}

TEST_CASE("Shadow camera reports the texel size the bias needs", "[renderer][shadow]")
{
	const std::vector<std::pair<glm::vec3, glm::vec3>> boxes = {
		{ { -5.0f, -10.0f, -1.0f }, { 5.0f, 10.0f, 1.0f } },
	};

	const ShadowCamera map = BuildShadowCamera(boxes, { 0.3f, 0.4f, 0.86f }, 2048.0f);
	REQUIRE(map.Valid);
	REQUIRE(map.TexelWorld > 0.0f);
	REQUIRE(map.TexelDepth > 0.0f);

	// A 2048-wide map covers the larger axis, so halving the resolution
	// quadruples the world size of a texel.
	const ShadowCamera coarseMap = BuildShadowCamera(boxes, { 0.3f, 0.4f, 0.86f }, 512.0f);
	REQUIRE(coarseMap.Valid);
	REQUIRE(coarseMap.TexelWorld == Approx(map.TexelWorld * 4.0f));

	// One NDC step spans half the depth range.
	REQUIRE(map.TexelDepth == Approx(2.0f / (map.FarPlane - map.NearPlane)));
}
