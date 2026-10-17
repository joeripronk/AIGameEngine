#include "fgpch.h"
#include "Forge/Renderer/EditorCamera.h"
#include "Platform/Vulkan/VulkanWindow.h"
#include "Forge/Events/MouseEvent.h"
#include "Forge/Events/KeyEvent.h"
#include "Forge/Core/Application.h"
#include "Forge/Core/Input.h"

namespace Forge {

	EditorCamera::EditorCamera(float fov, float nearClip, float farClip)
		: m_Fov(fov), m_NearClip(nearClip), m_FarClip(farClip)
	{
		UpdateProjection();
		Recalculate();
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
		m_Fov = fov;
		m_AspectRatio = aspectRatio > 0.0f ? aspectRatio : m_AspectRatio;
		m_NearClip = nearClip;
		m_FarClip = farClip;
		UpdateProjection();
		Recalculate();
	}

	void EditorCamera::SetViewportSize(float width, float height)
	{
		// A zero-sized viewport (a collapsed or minimised panel) is not an
		// aspect ratio. Keeping the previous one avoids a projection built on
		// a division by zero, which would poison every matrix derived from it.
		if (width <= 0.0f || height <= 0.0f)
			return;

		m_AspectRatio = width / height;
		UpdateProjection();
		Recalculate();
	}

	void EditorCamera::SetZoomLevel(float level)
	{
		m_ZoomLevel = glm::clamp(level, 0.1f, 10.0f);
		Recalculate();
	}

	void EditorCamera::UpdateProjection()
	{
		const float fov = glm::clamp(m_Fov / m_ZoomLevel, 1.0f, 179.0f);
		m_ProjectionMatrix = glm::perspective(glm::radians(fov), m_AspectRatio, m_NearClip, m_FarClip);
	}

	void EditorCamera::Recalculate()
	{
		glm::mat4 rotation = glm::toMat4(glm::quat(glm::radians(m_Rotation)));
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), m_Position);

		m_ViewMatrix = glm::inverse(translation * rotation);
		m_ViewProjectionMatrix = m_ProjectionMatrix * m_ViewMatrix;
	}

	void EditorCamera::OnUpdate(Timestep timestep)
	{
		const float ts = timestep.GetSeconds();

		// Camera movement (WASD + Q/E)
		float speed = 5.0f * ts;
		float zoomSpeed = 2.0f * ts;

		// Movement direction based on camera rotation (yaw)
		float yaw = glm::radians(m_Rotation.y);
		glm::vec3 forward = glm::vec3(-sin(yaw), 0.0f, -cos(yaw));
		glm::vec3 right = glm::vec3(cos(yaw), 0.0f, -sin(yaw));

		if (Input::IsKeyPressed(Key::W))
			m_Position += speed * forward;
		if (Input::IsKeyPressed(Key::S))
			m_Position -= speed * forward;
		if (Input::IsKeyPressed(Key::A))
			m_Position -= speed * right;
		if (Input::IsKeyPressed(Key::D))
			m_Position += speed * right;
		if (Input::IsKeyPressed(Key::Q))
			m_Position.y -= speed;
		if (Input::IsKeyPressed(Key::E))
			m_Position.y += speed;

		// Zoom (callback-based scroll offset). There may be no Application at all
		// when the camera is driven headlessly (tools, unit tests), and
		// Application::Get() dereferences a null or destroyed instance.
		if (!Application::HasInstance())
		{
			Recalculate();
			return;
		}

		glm::vec2 scrollOffset = { 0.0f, 0.0f };
		auto& vkWindowScope = Application::Get().GetWindowScope();
		if (vkWindowScope)
			scrollOffset = static_cast<VulkanWindow&>(*vkWindowScope).GetScrollOffset();

		if (scrollOffset.y != 0.0f)
			SetZoomLevel(m_ZoomLevel - zoomSpeed * scrollOffset.y);
		else
			Recalculate();
	}

	void EditorCamera::OnEvent(Event& event)
	{
		// Mouse look
		if (auto* mouseEvent = event.TryCast<MouseEvent>())
		{
			if (mouseEvent->GetButton() == Mouse::ButtonRight)
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