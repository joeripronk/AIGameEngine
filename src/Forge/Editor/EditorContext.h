#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Core/Timestep.h"
#include "Forge/Editor/Gizmo.h"
#include "Forge/Renderer/EditorCamera.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"

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
		bool OpenScene(const std::string& path);
		bool SaveScene(const std::string& path) const;

		// --- Selection --------------------------------------------------------
		Entity GetSelectedEntity() const { return m_SelectedEntity; }
		void SelectEntity(Entity entity);
		void ClearSelection();
		bool HasSelection() const { return m_SelectedEntity; }

		// --- Entity operations ------------------------------------------------
		Entity CreateEntity(const std::string& name = "Entity");
		void DestroyEntity(Entity entity);
		Entity DuplicateEntity(Entity entity);

		// --- Queries ----------------------------------------------------------
		Entity FindEntityByName(const std::string& name) const;
		Entity GetEntityByUUID(UUID uuid) const;
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

		// --- Asset / prefab ---------------------------------------------------
		Entity InstantiatePrefab(const std::string& prefabName, bool usePosition = false, const glm::vec3& position = glm::vec3(0.0f));

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
