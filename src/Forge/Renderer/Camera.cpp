#include "fgpch.h"
#include "Forge/Renderer/Camera.h"

namespace Forge {

	glm::mat4 BuildCubeFaceViewProjection(uint32_t face, const glm::vec3& origin,
		float nearClip, float farClip)
	{
		// (forward, up) per cube face, matching the six sampling directions of a
		// cubemap so the face a pixel samples is the face it was drawn into.
		constexpr glm::vec3 kForward[6] = {
			glm::vec3( 1.0f,  0.0f,  0.0f),
			glm::vec3(-1.0f,  0.0f,  0.0f),
			glm::vec3( 0.0f,  1.0f,  0.0f),
			glm::vec3( 0.0f, -1.0f,  0.0f),
			glm::vec3( 0.0f,  0.0f,  1.0f),
			glm::vec3( 0.0f,  0.0f, -1.0f),
		};
		constexpr glm::vec3 kUp[6] = {
			glm::vec3( 0.0f, -1.0f,  0.0f),
			glm::vec3( 0.0f, -1.0f,  0.0f),
			glm::vec3( 0.0f,  0.0f,  1.0f),
			glm::vec3( 0.0f,  0.0f, -1.0f),
			glm::vec3( 0.0f, -1.0f,  0.0f),
			glm::vec3( 0.0f, -1.0f,  0.0f),
		};

		if (face >= 6u)
			return glm::mat4(1.0f);

		const glm::mat4 view = glm::lookAt(origin, origin + kForward[face], kUp[face]);
		const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, nearClip, farClip);
		return projection * view;
	}

	Camera::Camera(float fov, float nearClip, float farClip)
	{
		SetProjection(fov, 16.0f / 9.0f, nearClip, farClip);
	}

	void Camera::SetPosition(const glm::vec3& position)
	{
		m_Position = position;
		Recalculate();
	}

	void Camera::SetRotation(const glm::vec3& rotation)
	{
		m_Rotation = rotation;
		Recalculate();
	}

	void Camera::SetProjection(float fov, float aspectRatio, float nearClip, float farClip)
	{
		m_NearClip = nearClip;
		m_FarClip = farClip;
		m_ProjectionMatrix = glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);
		Recalculate();
	}

	void Camera::Recalculate()
	{
		glm::mat4 rotation = glm::toMat4(glm::quat(glm::radians(m_Rotation)));
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_Position);

		m_ViewMatrix = glm::inverse(translation * rotation);
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	namespace Frustum {

		FrustumCorners CalculateCorners(const glm::mat4& viewProjection)
		{
			// Unproject the NDC cube (Hazel's method). The z values are the
			// OpenGL NDC range, which is what glm::perspective maps to.
			const glm::mat4 inv = glm::inverse(viewProjection);

			auto unproject = [&inv](float x, float y, float z)
			{
				glm::vec4 p = inv * glm::vec4(x, y, z, 1.0f);
				return glm::vec3(p) / p.w;
			};

			FrustumCorners corners;
			corners[(int)FrustumTypes::TopLeft] = unproject(-1.0f, 1.0f, -1.0f);
			corners[(int)FrustumTypes::TopRight] = unproject(1.0f, 1.0f, -1.0f);
			corners[(int)FrustumTypes::BottomLeft] = unproject(-1.0f, -1.0f, -1.0f);
			corners[(int)FrustumTypes::BottomRight] = unproject(1.0f, -1.0f, -1.0f);
			corners[(int)FrustumTypes::FarTopLeft] = unproject(-1.0f, 1.0f, 1.0f);
			corners[(int)FrustumTypes::FarTopRight] = unproject(1.0f, 1.0f, 1.0f);
			corners[(int)FrustumTypes::FarBottomLeft] = unproject(-1.0f, -1.0f, 1.0f);
			corners[(int)FrustumTypes::FarBottomRight] = unproject(1.0f, -1.0f, 1.0f);

			return corners;
		}

		namespace {

			// The six frustum planes, each with its normal pointing *outward*.
			// The faces are the four corner quads of the near/far/top/bottom/
			// left/right layout; the normal is oriented away from the frustum
			// interior so a positive dot product means "outside".
			struct FrustumPlane
			{
				glm::vec3 Point;
				glm::vec3 Normal;
			};

			std::array<FrustumPlane, 6> CalculatePlanes(const FrustumCorners& corners)
			{
				// Index quads per face, in FrustumTypes numbering.
				constexpr int kFaces[6][4] = {
					{ 0, 1, 2, 3 }, // near
					{ 4, 5, 6, 7 }, // far
					{ 0, 1, 4, 5 }, // top
					{ 2, 3, 6, 7 }, // bottom
					{ 0, 2, 4, 6 }, // left
					{ 1, 3, 5, 7 }, // right
				};

				glm::vec3 centre(0.0f);
				for (const glm::vec3& corner : corners)
					centre += corner;
				centre /= 8.0f;

				std::array<FrustumPlane, 6> planes;
				for (int f = 0; f < 6; ++f)
				{
					const glm::vec3& a = corners[kFaces[f][0]];
					const glm::vec3& b = corners[kFaces[f][1]];
					const glm::vec3& c = corners[kFaces[f][2]];

					glm::vec3 normal = glm::cross(b - a, c - a);
					if (glm::dot(normal, centre - a) > 0.0f)
						normal = -normal;

					planes[f] = { a, normal };
				}
				return planes;
			}

		}

		bool IntersectsPoint(const FrustumCorners& corners, const glm::vec3& point)
		{
			const auto planes = CalculatePlanes(corners);
			for (const FrustumPlane& plane : planes)
			{
				if (glm::dot(plane.Normal, point - plane.Point) > 0.0f)
					return false;
			}
			return true;
		}

		bool IntersectsAABB(const FrustumCorners& corners, const glm::vec3& min, const glm::vec3& max)
		{
			const auto planes = CalculatePlanes(corners);
			for (const FrustumPlane& plane : planes)
			{
				// The AABB corner *deepest against* the plane normal (the
				// "n-vertex"): the whole box is outside the plane exactly when
				// even that corner is past it. Testing the corner furthest along
				// the normal instead would cull every box that merely pokes
				// across a plane - which is the entire point of keeping them.
				glm::vec3 nVertex = max;
				for (int axis = 0; axis < 3; ++axis)
				{
					if (plane.Normal[axis] > 0.0f)
						nVertex[axis] = min[axis];
				}

				if (glm::dot(plane.Normal, nVertex - plane.Point) > 0.0f)
					return false;
			}
			return true;
		}

	}

}
