#include "fgpch.h"
#include "Forge/Renderer/EditorCamera.h"
#include "Platform/Vulkan/VulkanWindow.h"
#include "Forge/Core/Application.h"
#include "Forge/Core/Input.h"
#include "Forge/Core/MouseCodes.h"

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

	void EditorCamera::OnUpdate(Timestep timestep, bool acceptInput)
	{
		const float ts = timestep.GetSeconds();
		const float zoomSpeed = 2.0f * ts;

		// The pointer's position, tracked every frame: the mouse look is the
		// distance the pointer travelled since the last frame.
		const glm::vec2 pointer = Input::GetMousePosition();
		const bool hadPointer = m_HasLastPointerPosition;

		if (acceptInput)
		{
			// Camera movement (WASD + Q/E)
			const float speed = 5.0f * ts;

			// The movement is in the camera's own frame: W goes where the
			// camera looks, which is not the same as "north" once the camera
			// is pitched over.
			const glm::quat orientation = glm::quat(glm::radians(m_Rotation));
			const glm::vec3 forward = orientation * glm::vec3(0.0f, 0.0f, -1.0f);
			const glm::vec3 right = orientation * glm::vec3(1.0f, 0.0f, 0.0f);

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

			// Mouse look: the right button turns the camera. The pointer's
			// movement since the last frame is the turn, which is why the
			// position is remembered rather than read once.
			//
			// The view follows the pointer: dragging right looks right, and
			// dragging down looks down. The pitch is the engine's own
			// convention - positive is up, the way TransformComponent::Rotation
			// is - so the pointer moving down lowers the pitch.
			if (Input::IsMouseButtonPressed(Mouse::ButtonRight) && hadPointer)
			{
				const float dx = pointer.x - m_LastPointerPosition.x;
				const float dy = pointer.y - m_LastPointerPosition.y;
				m_Rotation.y -= dx * 0.1f;
				m_Rotation.x = glm::clamp(m_Rotation.x - dy * 0.1f, -89.0f, 89.0f);
			}
		}

		m_LastPointerPosition = pointer;
		m_HasLastPointerPosition = true;

		// Zoom (callback-based scroll offset). There may be no Application at all
		// when the camera is driven headlessly (tools, unit tests), and
		// Application::Get() dereferences a null or destroyed instance.
		if (Application::HasInstance())
		{
			glm::vec2 scrollOffset = { 0.0f, 0.0f };
			auto& vkWindowScope = Application::Get().GetWindowScope();
			if (vkWindowScope)
				scrollOffset = static_cast<VulkanWindow&>(*vkWindowScope).GetScrollOffset();

			if (acceptInput && scrollOffset.y != 0.0f)
				SetZoomLevel(m_ZoomLevel - zoomSpeed * scrollOffset.y);
		}

		Recalculate();
	}

}