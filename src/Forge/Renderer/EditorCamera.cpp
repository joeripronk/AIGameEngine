#include "fgpch.h"
#include "Forge/Renderer/EditorCamera.h"
#include "Forge/Events/MouseEvent.h"
#include "Forge/Events/KeyEvent.h"
#include "Forge/Core/Application.h"

// GLFW 3.6 mouse wheel support
#if !defined(GLFW_INCLUDE_VULKAN)
#include <GLFW/glfw3.h>
#endif

namespace Forge {

	EditorCamera::EditorCamera(float fov, float nearClip, float farClip)
	{
		SetProjection(fov, 16.0f / 9.0f, nearClip, farClip);
	}

	void EditorCamera::SetPosition(const glm::vec3& position)
	{
		m_Position = position;
		Recalculate();
	}

	void EditorCamera::SetRotation(const glm::vec3& rotation)
	{
		m_Rotation = rotation;
		Recalculate();
	}

	void EditorCamera::SetProjection(float fov, float aspectRatio, float nearClip, float farClip)
	{
		m_ProjectionMatrix = glm::perspective(glm::radians(fov), aspectRatio, nearClip, farClip);
		Recalculate();
	}

	void EditorCamera::Recalculate()
	{
		glm::mat4 rotation = glm::toMat4(glm::quat(glm::radians(m_Rotation)));
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_Position);

		m_ViewMatrix = glm::inverse(translation * rotation);
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	void EditorCamera::OnUpdate(float ts)
	{
		// Camera movement (WASD + Q/E)
		float speed = 5.0f * ts;
		float zoomSpeed = 2.0f * ts;

		GLFWwindow* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());

		// Movement direction based on camera rotation (yaw)
		float yaw = glm::radians(m_Rotation.y);
		glm::vec3 forward = glm::vec3(-sin(yaw), 0.0f, -cos(yaw));
		glm::vec3 right = glm::vec3(cos(yaw), 0.0f, -sin(yaw));

		if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
			m_Position += speed * forward;
		if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
			m_Position -= speed * forward;
		if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
			m_Position -= speed * right;
		if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
			m_Position += speed * right;
		if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS)
			m_Position.y -= speed;
		if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
			m_Position.y += speed;

		// Zoom
		m_ZoomLevel -= zoomSpeed * static_cast<float>(glfwGetMouseWheel(window));
		m_ZoomLevel = glm::clamp(m_ZoomLevel, 0.1f, 10.0f);

		Recalculate();
	}

	void EditorCamera::OnEvent(Event& event)
	{
		// Mouse look
		if (auto* mouseEvent = event.TryCast<MouseEvent>())
		{
			if (mouseEvent->GetButton() == GLFW_MOUSE_BUTTON_RIGHT)
			{
				float dx = mouseEvent->GetXOffset();
				float dy = mouseEvent->GetYOffset();
				m_Rotation.y += dx * 0.1f;
				m_Rotation.x += dy * 0.1f;
				m_Rotation.x = glm::clamp(m_Rotation.x, -89.0f, 89.0f);
			}
		}
	}

}
