#pragma once

#include "Forge/Renderer/Camera.h"

namespace Forge {

	class EditorCamera
	{
	public:
		EditorCamera(float fov = 45.0f, float nearClip = 0.1f, float farClip = 1000.0f);

		void SetPosition(const glm::vec3& position);
		void SetRotation(const glm::vec3& rotation);
		void SetProjection(float fov, float aspectRatio, float nearClip, float farClip);

		const glm::vec3& GetPosition() const { return m_Position; }
		const glm::vec3& GetRotation() const { return m_Rotation; }
		const glm::mat4& GetViewMatrix() const { return m_ViewMatrix; }
		const glm::mat4& GetProjectionMatrix() const { return m_ProjectionMatrix; }
		const glm::mat4& GetViewProjectionMatrix() const { return m_ViewProjectionMatrix; }

		void OnUpdate(float ts);
		void OnEvent(class Event& event);

		void SetZoomLevel(float level) { m_ZoomLevel = level; }
		float GetZoomLevel() const { return m_ZoomLevel; }

		glm::vec3 GetCameraPosition() const { return m_Position; }
		glm::vec3 GetCameraRotation() const { return m_Rotation; }

	private:
		void Recalculate();

		glm::vec3 m_Position = { 0.0f, 4.0f, 10.0f };
		glm::vec3 m_Rotation = { 0.0f, 0.0f, 0.0f };
		glm::mat4 m_ViewMatrix;
		glm::mat4 m_ProjectionMatrix;
		glm::mat4 m_ViewProjectionMatrix;

		float m_ZoomLevel = 1.0f;
		float m_AspectRatio = 16.0f / 9.0f;
	};

}
