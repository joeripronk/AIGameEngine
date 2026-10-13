#include "fgpch.h"
#include "Forge/Renderer/EditorCamera.h"
#include "Forge/Events/MouseEvent.h"
#include "Forge/Events/KeyEvent.h"

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
		// TODO: Handle camera movement (WASD, mouse look)
		// float speed = 5.0f * ts;
		// if (Input::IsKeyPressed(KEY_W)) m_Position += speed * GetCameraDirection();
		// if (Input::IsKeyPressed(KEY_S)) m_Position -= speed * GetCameraDirection();
		// if (Input::IsKeyPressed(KEY_A)) m_Position -= speed * GetCameraRight();
		// if (Input::IsKeyPressed(KEY_D)) m_Position += speed * GetCameraRight();
		// if (Input::IsKeyPressed(KEY_Q)) m_Position -= speed * glm::vec3(0, 1, 0);
		// if (Input::IsKeyPressed(KEY_E)) m_Position += speed * glm::vec3(0, 1, 0);

		Recalculate();
	}

	void EditorCamera::OnEvent(Event& event)
	{
		// TODO: Handle mouse events for camera rotation
	}

}
