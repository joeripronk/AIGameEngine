#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Core/Timestep.h"
#include "Forge/Editor/Gizmo.h"
#include "Forge/Editor/UndoStack.h"
#include "Forge/Renderer/EditorCamera.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Project/Project.h"

#include <filesystem>
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

		// Viewport picking (the Hazel SceneTree pattern): selects the closest
		// entity the ray hits and reports whether anything was hit. The
		// selection is left alone on a miss - clicking empty space in a 3D
		// viewport is usually a camera drag, not a request to lose the selection.
		[[nodiscard]]
		bool SelectEntityAtRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1000.0f);

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
		void Simulate();
		void Pause();
		void Stop();
		void Step(int frames = 1);
		bool IsPlaying() const { return m_IsPlaying; }
		bool IsPaused() const { return m_IsPaused; }
		// Simulate runs the scene's scripts on the live editor scene while the
		// viewport watches through the scene's own primary camera. Play runs the
		// scripts under the editor camera, which the author steers by hand.
		bool IsSimulate() const { return m_IsSimulate; }
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
		// The matrix the viewport renders with: the editor camera, except while
		// simulating, where the scene's primary camera drives the view.
		glm::mat4 GetViewProjection() const;

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

		// --- Component types (RTTI) -------------------------------------------
		// The registered component type names, in registration order - the
		// agent's answer to "what can I add", read from the same registry the
		// engine uses, not a list duplicated in tooling.
		std::vector<std::string> GetRegisteredComponentTypes() const;

		// Add / remove a component by its registered name. True means the
		// component changed; false means nothing changed (unknown name, already
		// present, already absent, or a structural component - which every
		// entity must keep, see the Remove Component menu rules).
		//
		// Both are undoable: a component that was added by mistake is one
		// Ctrl+Z away from not being there.
		[[nodiscard]] bool AddComponentByName(Entity entity, const std::string& name);
		[[nodiscard]] bool RemoveComponentByName(Entity entity, const std::string& name);

		// --- Undo / redo ------------------------------------------------------

		// Every change the editor makes goes through a command, so it can be
		// taken back. The changes that are made outside the editor's own
		// operations - a property dragged in the inspector, a gizmo drag -
		// are wrapped by the editor in BeginEntityEdit / CommitEntityEdit.
		//
		// False when there was nothing to undo.
		[[nodiscard]] bool Undo();
		[[nodiscard]] bool Redo();

		[[nodiscard]] bool CanUndo() const { return m_UndoStack.CanUndo(); }
		[[nodiscard]] bool CanRedo() const { return m_UndoStack.CanRedo(); }

		// What the menu calls the change that would be undone or redone.
		[[nodiscard]] std::string GetUndoName() const { return m_UndoStack.GetUndoName(); }
		[[nodiscard]] std::string GetRedoName() const { return m_UndoStack.GetRedoName(); }

		// The history of the scene. A new scene starts with an empty one:
		// the commands of the old scene would undo changes to a scene that
		// is not there.
		[[nodiscard]] size_t GetUndoCount() const { return m_UndoStack.GetUndoCount(); }
		[[nodiscard]] size_t GetRedoCount() const { return m_UndoStack.GetRedoCount(); }

		// Wraps an edit that the editor makes to an entity: the inspector
		// edits the components in place, and there is no other way to know
		// what the entity looked like before.
		//
		// BeginEntityEdit remembers the state, CommitEntityEdit compares it
		// with the state the edit left behind and pushes a command when they
		// differ. An edit that changed nothing pushes nothing.
		void BeginEntityEdit(Entity entity);
		void CommitEntityEdit();

		// A transform change, as the gizmo makes it. A drag is one change:
		// the commands of a drag merge with each other.
		//
		// Where the entity was is the caller's to say - the gizmo knows
		// where the entity was when the drag started, and the scene only
		// knows where it is now.
		void RecordTransformChange(Entity entity, const glm::vec3& beforeTranslation,
			const glm::vec3& beforeRotation, const glm::vec3& beforeScale);

	private:
		Ref<Scene> m_Scene;
		Entity m_SelectedEntity;
		EditorCamera m_Camera;

		bool m_IsPlaying = false;
		bool m_IsPaused = false;
		bool m_IsSimulate = false;

		Gizmo::Mode m_GizmoMode = Gizmo::Mode::Translate;
		Gizmo::Space m_GizmoSpace = Gizmo::Space::World;
		float m_TranslationSnap = 0.0f;

		// The history of the scene.
		UndoStack m_UndoStack;

		// The entity an edit is being made to, and the state it was in
		// before the edit. The state is empty when no edit is in progress.
		Entity m_EditingEntity;
		std::string m_EditStateBefore;
		std::string m_EditStateAfter;

		// Pushes a command for the entity's current state, if it differs
		// from the state it was in. The state is the entity's serialized
		// form, so one command covers every component there is.
		void RecordEntityStateChange(Entity entity, const std::string& before, const std::string& description);

		// Records the scene in the active project, so that reopening the
		// project reopens the scene.
		void RecordSceneInProject(const std::string& path) const;

		// Takes a component off an entity without recording a change of its
		// own. The caller is a command that is restoring a state, and a
		// command that pushed another command would undo itself.
		[[nodiscard]] bool RemoveComponentQuietly(Entity entity, const std::string& name);
	};

}
