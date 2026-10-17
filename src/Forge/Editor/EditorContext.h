#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Core/Timestep.h"
#include "Forge/Editor/Gizmo.h"
#include "Forge/Renderer/EditorCamera.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Project/Project.h"

#include <vector>

namespace Forge {

	// A window-independent editor model. It owns a scene, tracks selection and
	// playback state, and exposes every operation an editor UI or an AI agent
	// needs. The ImGui EditorApp is a thin wrapper around this class, and the
	// unit tests drive it directly.
	class EditorContext
	{
	public:
		EditorContext();
		~EditorContext();

		// --- Scene management -------------------------------------------------
		Ref<Scene> GetScene() const { return m_Scene; }
		void NewScene();
		[[nodiscard]] bool OpenScene(const std::string& path);
		[[nodiscard]] bool SaveScene(const std::string& path) const;

		// --- Selection --------------------------------------------------------
		Entity GetSelectedEntity() const { return m_SelectedEntity; }
		void SelectEntity(Entity entity);
		void ClearSelection();
		bool HasSelection() const { return m_SelectedEntity; }

		// --- Entity operations ------------------------------------------------
		Entity CreateEntity(const std::string& name = "Entity");
		void DestroyEntity(Entity entity);
		[[nodiscard]] Entity DuplicateEntity(Entity entity);

		// --- Queries ----------------------------------------------------------
		[[nodiscard]] Entity FindEntityByName(const std::string& name) const;
		[[nodiscard]] Entity GetEntityByUUID(UUID uuid) const;
		std::vector<Entity> GetAllEntities() const;

		// --- Playback ---------------------------------------------------------
		void Play();
		void Pause();
		void Stop();
		void Step(int frames = 1);
		bool IsPlaying() const { return m_IsPlaying; }
		bool IsPaused() const { return m_IsPaused; }
		// Toggle play/pause; used by UI checkbox bindings.
		void SetPlaying(bool playing);

		// --- Per-frame update -------------------------------------------------
		void Update(Timestep ts);

		// --- Gizmo ------------------------------------------------------------
		void SetGizmoMode(Gizmo::Mode mode) { m_GizmoMode = mode; }
		Gizmo::Mode GetGizmoMode() const { return m_GizmoMode; }
		void SetGizmoSpace(Gizmo::Space space) { m_GizmoSpace = space; }
		Gizmo::Space GetGizmoSpace() const { return m_GizmoSpace; }
		void SetTranslationSnap(float step) { m_TranslationSnap = step; }
		float GetTranslationSnap() const { return m_TranslationSnap; }

		// --- Camera -----------------------------------------------------------
		EditorCamera& GetCamera() { return m_Camera; }
		const glm::mat4& GetViewProjection() const { return m_Camera.GetViewProjectionMatrix(); }

		// --- Project ----------------------------------------------------------
		// A project records where the assets are and which scene the game starts
		// in. An agent that builds a game needs both to be able to save the
		// result as something a runtime can open, not just a scene file.
		Ref<Project> NewProject(const std::string& name = "Untitled");
		[[nodiscard]] bool OpenProject(const std::string& path);
		[[nodiscard]] bool SaveProject(const std::string& path) const;

		// Null when no project file is open, which is a legitimate state for a
		// headless agent that only wants to edit a scene.
		const ProjectConfig* GetProjectConfig() const;

		// Resolves a project-relative path (an asset, a scene) to something
		// openable. Falls back to the path itself when no project is open, so
		// the same call works before and after a project is created.
		[[nodiscard]] std::string ResolveProjectPath(const std::string& relative) const;

		// --- Asset / prefab ---------------------------------------------------
		[[nodiscard]] Entity InstantiatePrefab(const std::string& prefabName, bool usePosition = false, const glm::vec3& position = glm::vec3(0.0f));

	private:
		Ref<Scene> m_Scene;
		Entity m_SelectedEntity;
		EditorCamera m_Camera;

		bool m_IsPlaying = false;
		bool m_IsPaused = false;

		Gizmo::Mode m_GizmoMode = Gizmo::Mode::Translate;
		Gizmo::Space m_GizmoSpace = Gizmo::Space::World;
		float m_TranslationSnap = 0.0f;
	};

}
