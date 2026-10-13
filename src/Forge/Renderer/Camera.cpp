#include "fgpch.h"
#include "Forge/Renderer/Camera.h"

namespace Forge {

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
