#pragma once

#include <glm/glm.hpp>

namespace Forge {

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

		void Recalculate();
	private:
		glm::vec3 m_Position = { 0.0f, 0.0f, 5.0f };
		glm::vec3 m_Rotation = { 0.0f, 0.0f, 0.0f };
		glm::mat4 m_ViewMatrix;
		glm::mat4 m_ProjectionMatrix;
		glm::mat4 m_ViewProjectionMatrix;
	};

}
