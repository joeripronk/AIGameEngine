#pragma once

#include "Forge/Core/Timestep.h"
#include "Forge/Renderer/Camera.h"

namespace Forge {

	class EditorCamera
	{
	public:
		EditorCamera(float fov = 45.0f, float nearClip = 0.1f, float farClip = 1000.0f);

		void SetPosition(const glm::vec3& position);
		void SetRotation(const glm::vec3& rotation);
		void SetProjection(float fov, float aspectRatio, float nearClip, float farClip);

		// Keeps the projection's aspect ratio equal to the viewport it draws
		// into. Without it the projection stays at the 16:9 it was constructed
		// with, while the editor picks gizmos against the real panel size, so
		// every drag picks somewhere other than where the gizmo is drawn.
		void SetViewportSize(float width, float height);

		const glm::vec3& GetPosition() const { return m_Position; }
		const glm::vec3& GetRotation() const { return m_Rotation; }
		const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
		const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
		const glm::mat4& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }

		void OnUpdate(Timestep ts);
		void OnEvent(class Event& event);

		// A multiplier on the field of view: 1 is the authored FOV, 2 is half
		// it (narrower, so closer). It is applied in Recalculate rather than
		// only stored - a zoom level nothing reads is a lie told to whoever
		// scrolls the wheel.
		void SetZoomLevel(float level);
		float GetZoomLevel() const { return m_ZoomLevel; }

		float GetAspectRatio() const { return m_AspectRatio; }

		glm::vec3 GetCameraPosition() const { return m_Position; }
		glm::vec3 GetCameraRotation() const { return m_Rotation; }

	private:
		void Recalculate();
		void UpdateProjection();

		glm::vec3 m_Position = { 0.0f, 4.0f, 10.0f };
		glm::vec3 m_Rotation = { 0.0f, 0.0f, 0.0f };
		glm::mat4 m_ViewMatrix;
		glm::mat4 m_ProjectionMatrix;
		glm::mat4 m_ViewProjectionMatrix;

		float m_ZoomLevel = 1.0f;
		float m_AspectRatio = 16.0f / 9.0f;

		// The authored projection parameters. The aspect ratio changes every
		// time the viewport is resized, so the FOV and clips are kept to
		// rebuild from rather than being folded into a matrix nobody can update.
		float m_Fov = 45.0f;
		float m_NearClip = 0.1f;
		float m_FarClip = 1000.0f;
	};

}