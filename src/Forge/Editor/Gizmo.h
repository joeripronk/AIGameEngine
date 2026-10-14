#pragma once

#include "Forge/Core/Base.h"

#include <glm/glm.hpp>

namespace Forge {

	// Pure-math gizmo helpers for translating, rotating and scaling entities via
	// pointer input. Kept free of any rendering/window dependency so it can be
	// unit-tested and reused by both the ImGui editor and headless tooling.
	class Gizmo
	{
	public:
		enum class Mode { Translate, Rotate, Scale };
		enum class Space { Local, World };

		// Intersects a ray (origin + normalized direction) with the plane that
		// passes through `planePoint` and has the given normal. Returns the
		// hit point, or nullptr when the ray is parallel to the plane.
		static const glm::vec3* RayPlaneIntersect(const glm::vec3& origin, const glm::vec3& direction,
			const glm::vec3& planePoint, const glm::vec3& planeNormal, glm::vec3& outHit);

		// Distance in world units from a ray to a point (used for axis picking).
		static float RayPointDistance(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& point);

		// Projects a world-space point onto an axis line and returns the
		// signed distance along the axis from `axisOrigin`.
		static float ProjectOntoAxis(const glm::vec3& point, const glm::vec3& axisOrigin, const glm::vec3& axisDirection);

		// Snaps a value to the nearest multiple of `step` (returns `value`
		// unchanged when `step` is zero).
		static float Snap(float value, float step);

		// Snaps a translation delta component-wise.
		static glm::vec3 Snap(const glm::vec3& value, float step);
	};

}
