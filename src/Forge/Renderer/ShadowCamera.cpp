#include "fgpch.h"
#include "Forge/Renderer/ShadowCamera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <limits>

namespace Forge {

	ShadowBias ComputeShadowBias(float texelWorld, float texelDepth,
		float constantWorldBias, float slopeScale, float maxNormalOffset)
	{
		ShadowBias bias;

		// A texel size that is not a positive number means there is no map to
		// bias against (no camera, no resolution). Reporting a bias for it would
		// hand the shader a number describing a volume that does not exist.
		if (!(texelWorld > 0.0f) || !(texelDepth > 0.0f) ||
			!std::isfinite(texelWorld) || !std::isfinite(texelDepth))
			return bias;

		bias.NormalOffsetWorld = texelWorld * maxNormalOffset;
		bias.DepthBiasNdc = (constantWorldBias + texelWorld * slopeScale) * texelDepth;
		return bias;
	}

	ShadowCamera BuildShadowCamera(const std::vector<std::pair<glm::vec3, glm::vec3>>& boxes,
		const glm::vec3& lightDirection, float mapResolution)
	{
		ShadowCamera camera;

		if (boxes.empty())
			return camera;

		const float length = glm::length(lightDirection);
		// Not `length < 1e-6f`: a NaN length compares false against everything,
		// so a component that is NaN would sail through as a direction.
		if (!(length > 1e-6f) || !std::isfinite(length))
			return camera;

		const glm::vec3 toLight = lightDirection / length;

		glm::vec3 mins = boxes.front().first;
		glm::vec3 maxs = boxes.front().second;
		for (const auto& box : boxes)
		{
			mins = glm::min(mins, box.first);
			maxs = glm::max(maxs, box.second);
		}

		const glm::vec3 centre = (mins + maxs) * 0.5f;
		const glm::vec3 halfExtent = (maxs - mins) * 0.5f;

		// A flat scene (a quad, a line) has no span on one axis, and an ortho
		// volume with a zero span has no inverse - so the shadow lookup and the
		// map would disagree about every fragment on the first flat model.
		const glm::vec3 radius = glm::max(halfExtent, glm::vec3(0.5f));

		// The eye sits on the lit side of the scene, far enough back that the
		// whole scene is in front of the near plane.
		const float distance = glm::length(radius) * 2.0f + 1.0f;
		const glm::vec3 eye = centre + toLight * distance;

		// The world axis least aligned with the light. `lookAt` is undefined
		// when up is parallel to the view direction, and a light straight down
		// (0, -1, 0) is the common case in a game, not an edge case.
		const glm::vec3 absolute = glm::abs(toLight);
		const glm::vec3 up = (absolute.x <= absolute.y && absolute.x <= absolute.z)
			? glm::vec3(1.0f, 0.0f, 0.0f)
			: (absolute.y <= absolute.z ? glm::vec3(0.0f, 1.0f, 0.0f)
										: glm::vec3(0.0f, 0.0f, 1.0f));

		const glm::mat4 view = glm::lookAt(eye, centre, up);

		// Frame the volume on the scene's own corners measured in light space,
		// not on the world AABB: a rotated scene is not axis-aligned, and a
		// volume fitted to an axis-aligned box leaves the corners of a rotated
		// scene outside it.
		glm::vec3 lo(std::numeric_limits<float>::max());
		glm::vec3 hi(std::numeric_limits<float>::lowest());
		for (int i = 0; i < 8; ++i)
		{
			const glm::vec3 corner(
				(i & 1) ? maxs.x : mins.x,
				(i & 2) ? maxs.y : mins.y,
				(i & 4) ? maxs.z : mins.z);
			const glm::vec3 inLightSpace = view * glm::vec4(corner, 1.0f);
			lo = glm::min(lo, inLightSpace);
			hi = glm::max(hi, inLightSpace);
		}

		// A margin, because the map is sampled with a filter: a fragment exactly
		// on the edge reads a half texel outside it.
		const float margin = 0.05f;
		lo.x -= margin;
		hi.x += margin;
		lo.y -= margin;
		hi.y += margin;

		// Light space looks down -Z, so depth is -z and the near plane is the
		// shallowest corner. The margin applies to depth as well: a corner
		// exactly on the near plane lands exactly on the edge of the depth
		// range, and float rounding puts it on whichever side of the edge the
		// driver feels like that day.
		const float nearPlane = glm::max(-hi.z - margin, 0.05f);
		const float farPlane = glm::max(-lo.z + margin, nearPlane + 0.05f) + margin;

		camera.ViewProjection = glm::ortho(lo.x, hi.x, lo.y, hi.y, nearPlane, farPlane) * view;
		camera.NearPlane = nearPlane;
		camera.FarPlane = farPlane;

		// A square map covers its larger extent across `mapResolution` texels, so
		// the smaller axis lands on a fraction of a texel - the coarse axis is
		// the one that decides how far a texel reaches in the world.
		const float extent = glm::max(hi.x - lo.x, hi.y - lo.y);
		if (mapResolution > 0.0f && std::isfinite(mapResolution))
			camera.TexelWorld = extent / mapResolution;
		camera.TexelDepth = 2.0f / (farPlane - nearPlane);

		camera.Valid = true;
		return camera;
	}

}
