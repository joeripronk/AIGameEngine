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

}
