#pragma once

#include <glm/glm.hpp>

#include <array>

namespace Forge {

	// The eight corners of a view frustum, in Hazel's layout: the four near
	// corners (top-left, top-right, bottom-left, bottom-right) followed by the
	// four far corners in the same order. Culling needs geometry, not planes -
	// corners are what the editor can draw and what a test can assert on.
	enum class FrustumTypes
	{
		TopLeft = 0,
		TopRight = 1,
		BottomLeft = 2,
		BottomRight = 3,
		FarTopLeft = 4,
		FarTopRight = 5,
		FarBottomLeft = 6,
		FarBottomRight = 7
	};

	using FrustumCorners = std::array<glm::vec3, 8>;

	namespace Frustum {

		// The corners of the frustum defined by a view-projection matrix, found
		// by unprojecting the eight NDC cube corners (Hazel's method). The clip
		// distances are encoded in the projection matrix itself - the NDC near
		// and far planes land exactly on them - so the matrix is all this needs.
		FrustumCorners CalculateCorners(const glm::mat4& viewProjection);

		// A point is inside when it is on the inner side of all six frustum
		// planes.
		bool IntersectsPoint(const FrustumCorners& corners, const glm::vec3& point);

		// An AABB is culled only when a single plane leaves it entirely outside
		// - the test uses the AABB corner deepest against each plane's normal,
		// so a box that straddles a plane counts as inside.
		bool IntersectsAABB(const FrustumCorners& corners, const glm::vec3& min, const glm::vec3& max);

	}

	// A 90-degree perspective view-projection looking from `origin` along one of
	// the six cube-face directions: 0 = +X, 1 = -X, 2 = +Y, 3 = -Y, 4 = +Z,
	// 5 = -Z. Faces outside that range return an identity matrix, which is a
	// degenerate view that renders nothing - callers must validate the index.
	glm::mat4 BuildCubeFaceViewProjection(uint32_t face, const glm::vec3& origin,
		float nearClip = 0.1f, float farClip = 100.0f);

	class Camera
	{
	public:
		Camera(float fov = 45.0f, float nearClip = 0.1f, float farClip = 1000.0f);

		void SetPosition(const glm::vec3& position);
		void SetRotation(const glm::vec3& rotation);
		void SetProjection(float fov, float aspectRatio, float nearClip, float farClip);

		const glm::vec3& GetPosition() const { return m_Position; }
		const glm::vec3& GetRotation() const { return m_Rotation; }
		const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
		const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
		const glm::mat4& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }
		float GetNearClip() const { return m_NearClip; }
		float GetFarClip() const { return m_FarClip; }

		// The view frustum in world space, for culling.
		FrustumCorners GetFrustumCorners() const { return Frustum::CalculateCorners(m_ViewProjectionMatrix); }

		void Recalculate();
	private:
		glm::vec3 m_Position = { 0.0f, 0.0f, 5.0f };
		glm::vec3 m_Rotation = { 0.0f, 0.0f, 0.0f };
		glm::mat4 m_ViewMatrix;
		glm::mat4 m_ProjectionMatrix;
		glm::mat4 m_ViewProjectionMatrix;
		float m_NearClip = 0.1f;
		float m_FarClip = 1000.0f;
	};

}
