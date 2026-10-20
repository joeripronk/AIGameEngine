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

	bool Gizmo::ClosestPointOnAxis(const glm::vec3& rayOrigin, const glm::vec3& rayDirection,
		const glm::vec3& axisOrigin, const glm::vec3& axisDirection,
		glm::vec3& outPoint, float& outDistance)
	{
		// The closest points of two lines: the axis line (axisOrigin, u) and
		// the ray (rayOrigin, v). The standard solution - solve for the two
		// parameters that make the connecting vector perpendicular to both.
		const glm::vec3 w0 = axisOrigin - rayOrigin;
		const float a = glm::dot(axisDirection, axisDirection);
		const float b = glm::dot(axisDirection, rayDirection);
		const float c = glm::dot(rayDirection, rayDirection);
		const float d = glm::dot(axisDirection, w0);
		const float e = glm::dot(rayDirection, w0);

		const float denominator = a * c - b * b;
		if (std::abs(denominator) < 1e-6f)
			return false;  // the ray runs along the axis: no single closest point

		const float alongAxis = (b * e - c * d) / denominator;
		const float alongRay = (a * e - b * d) / denominator;

		const glm::vec3 axisPoint = axisOrigin + axisDirection * alongAxis;
		const glm::vec3 rayPoint = rayOrigin + rayDirection * alongRay;

		outPoint = axisPoint;
		outDistance = glm::length(rayPoint - axisPoint);
		return true;
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
