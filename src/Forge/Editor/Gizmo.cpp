#include "fgpch.h"
#include "Forge/Editor/Gizmo.h"

#include <cmath>

namespace Forge {

	const glm::vec3* Gizmo::RayPlaneIntersect(const glm::vec3& origin, const glm::vec3& direction,
		const glm::vec3& planePoint, const glm::vec3& planeNormal, glm::vec3& outHit)
	{
		float denom = glm::dot(planeNormal, direction);
		if (std::abs(denom) < 1e-6f)
			return nullptr;

		float t = glm::dot(planeNormal, planePoint - origin) / denom;
		outHit = origin + direction * t;
		return &outHit;
	}

	float Gizmo::RayPointDistance(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& point)
	{
		glm::vec3 toPoint = point - origin;
		float t = glm::dot(toPoint, direction);
		glm::vec3 closest = origin + direction * t;
		return glm::length(point - closest);
	}

	float Gizmo::ProjectOntoAxis(const glm::vec3& point, const glm::vec3& axisOrigin, const glm::vec3& axisDirection)
	{
		return glm::dot(point - axisOrigin, axisDirection);
	}

	float Gizmo::Snap(float value, float step)
	{
		if (step <= 0.0f)
			return value;

		return std::round(value / step) * step;
	}

	glm::vec3 Gizmo::Snap(const glm::vec3& value, float step)
	{
		return glm::vec3(Snap(value.x, step), Snap(value.y, step), Snap(value.z, step));
	}

}
