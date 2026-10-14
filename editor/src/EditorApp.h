#pragma once

#include "Forge/Core/Application.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Renderer/EditorCamera.h"

namespace Forge {

	class EditorApp : public Application
	{
	public:
		EditorApp();
		~EditorApp() override;

		void Run() override;

		// Scene management
		Ref<Scene> GetScene() { return m_Scene; }

		// Editor state
		Entity GetSelectedEntity() const { return m_SelectedEntity; }
		void SetSelectedEntity(Entity entity) { m_SelectedEntity = entity; }

		void DrawEditorUI(Timestep ts);

	private:
		void CreateDefaultScene();

		Ref<Scene> m_Scene;
		Entity m_SelectedEntity;
		EditorCamera m_Camera;
		bool m_IsPlaying = false;
		bool m_IsPaused = false;
	};

}
