#include "fgpch.h"
#include "Forge/Editor/EditorContext.h"

#include "Forge/Editor/EditorCommands.h"

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Scene/Prefab.h"
#include "Forge/Physics/Physics.h"
#include "Forge/Core/RTTI.h"

namespace Forge {

	EditorContext::EditorContext()
	{
		m_Scene = CreateRef<Scene>();
	}

	EditorContext::~EditorContext()
	{
		Stop();
	}

	void EditorContext::NewScene()
	{
		Stop();
		m_Scene = CreateRef<Scene>();
		m_SelectedEntity = {};
		// The commands of the old scene would undo changes to a scene that
		// is not there any more.
		m_UndoStack.Clear();
	}

	bool EditorContext::OpenScene(const std::string& path)
	{
		auto scene = CreateRef<Scene>();
		SceneSerializer serializer(scene);
		if (!serializer.Deserialize(path))
			return false;

		Stop();
		m_Scene = scene;
		m_SelectedEntity = {};
		m_UndoStack.Clear();
		return true;
	}

	bool EditorContext::SaveScene(const std::string& path) const
	{
		if (!m_Scene)
			return false;

		// Serialize reports failure; forwarding it means an agent that saves a
		// scene learns whether the save actually happened.
		SceneSerializer serializer(m_Scene);
		if (!serializer.Serialize(path))
			return false;

		// The scene is recorded in the project, so that reopening the project
		// reopens the scene. A project that does not know its own scene is a
		// project the editor cannot come back to.
		RecordSceneInProject(path);
		return true;
	}

	void EditorContext::RecordSceneInProject(const std::string& path) const
	{
		Ref<Project> project = Project::GetActive();
		if (!project)
			return;  // a scene without a project is a scene on its own

		// The path is relative to the project, so the project can be moved
		// or checked out anywhere. A project in the working directory has
		// no directory of its own, and the path is already relative.
		const std::filesystem::path directory = project->GetProjectDirectory();
		project->GetConfig().EditorScene = directory.empty()
			? std::filesystem::path(path)
			: std::filesystem::relative(path, directory);

		// And the project file is written back, so the change survives the
		// session. A project that has never been saved has nowhere to be
		// written to.
		if (!project->GetProjectPath().empty())
			(void)Project::Save(project, project->GetProjectPath());
	}

	void EditorContext::SelectEntity(Entity entity)
	{
		m_SelectedEntity = entity;
	}

	bool EditorContext::SelectEntityAtRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance)
	{
		Entity hit = m_Scene->FindEntityOnRay(origin, direction, maxDistance);
		if (!hit)
			return false;

		m_SelectedEntity = hit;
		return true;
	}

	void EditorContext::ClearSelection()
	{
		m_SelectedEntity = {};
	}

	Entity EditorContext::CreateEntity(const std::string& name)
	{
		Entity entity = m_Scene->CreateEntity(name);
		if (entity)
		{
			// The entity did not exist before, so the "before" state is
			// empty: the undo takes it away again.
			RecordEntityStateChange(entity, {}, "Create Entity");
		}
		return entity;
	}

	void EditorContext::DestroyEntity(Entity entity)
	{
		// Only a real destroy can invalidate the selection. Passing an already
		// invalid entity must be a no-op, not a way to silently clear selection.
		if (!entity)
			return;

		if (m_SelectedEntity == entity)
			m_SelectedEntity = {};

		// The state of the entity is kept, so the undo brings it back - with
		// the components it had and the identity it had.
		const std::string before = SceneSerializer::SerializeEntityToString(entity);
		const UUID uuid = entity.GetUUID();

		m_Scene->DestroyEntity(entity);

		if (!before.empty())
		{
			auto command = std::make_unique<EntityStateCommand>(m_Scene.get(), uuid, before, std::string(),
				[this](Entity target, const std::string& component) { return RemoveComponentQuietly(target, component); });
			command->SetDescription("Delete Entity");
			m_UndoStack.Push(std::move(command));
		}
	}

	Entity EditorContext::DuplicateEntity(Entity entity)
	{
		Entity copy = m_Scene->DuplicateEntity(entity);
		if (copy)
		{
			RecordEntityStateChange(copy, {}, "Duplicate Entity");
		}
		return copy;
	}

	Entity EditorContext::FindEntityByName(const std::string& name) const
	{
		return m_Scene->FindEntityByName(name);
	}

	Entity EditorContext::GetEntityByUUID(UUID uuid) const
	{
		return m_Scene->GetEntityByUUID(uuid);
	}

	std::vector<Entity> EditorContext::GetAllEntities() const
	{
		std::vector<Entity> entities;
		auto view = m_Scene->GetAllEntitiesWith<TagComponent>();
		for (auto e : view)
			entities.push_back(Entity{ e, m_Scene.get() });
		return entities;
	}

	void EditorContext::Play()
	{
		// Playing while paused means resume, so clear the paused flag rather
		// than returning early and leaving the caller stuck.
		if (m_IsPlaying)
		{
			// Play from a simulate session takes the camera back under the
			// author's control; the scripts keep running.
			m_IsSimulate = false;
			m_IsPaused = false;
			m_Scene->SetPaused(false);
			return;
		}

		m_IsPlaying = true;
		m_IsSimulate = false;
		m_IsPaused = false;

		// The Scene holds its own pause flag and OnUpdateRuntime reads it, so
		// the context and the scene must be told together. Pause() may have been
		// called before playback ever started.
		m_Scene->SetPaused(false);
		m_Scene->OnRuntimeStart();
	}

	void EditorContext::Simulate()
	{
		// The toolbar's Simulate button doubles as Stop while simulating.
		if (m_IsPlaying && m_IsSimulate)
		{
			Stop();
			return;
		}

		// Entering simulate from play mode tears the play session down first so
		// the scripts see OnRuntimeStop/OnRuntimeStart for the new mode, as
		// leaving Play does in the Hazel editor.
		if (m_IsPlaying)
			Stop();

		m_IsPlaying = true;
		m_IsSimulate = true;
		m_IsPaused = false;
		m_Scene->SetPaused(false);
		m_Scene->OnRuntimeStart();
	}

	void EditorContext::Pause()
	{
		// Pausing is only meaningful during playback; outside it, toggling would
		// leave a stale flag that Play() would then have to undo.
		if (!m_IsPlaying)
			return;

		m_IsPaused = !m_IsPaused;
		m_Scene->SetPaused(m_IsPaused);
	}

	void EditorContext::Stop()
	{
		if (!m_IsPlaying)
			return;

		m_Scene->OnRuntimeStop();
		m_IsPlaying = false;
		m_IsPaused = false;
		m_IsSimulate = false;
		m_Scene->SetPaused(false);
	}

	void EditorContext::SetPlaying(bool playing)
	{
		if (playing)
			Play();
		else
			Stop();
	}

	void EditorContext::Step(int frames)
	{
		m_Scene->Step(frames);
	}

	glm::mat4 EditorContext::GetViewProjection() const
	{
		// Simulate watches the scene through its own camera: script motion moves
		// the view. Play and edit keep the author-steered editor camera.
		if (m_IsSimulate && m_Scene)
		{
			Entity camera = m_Scene->GetPrimaryCameraEntity();
			if (camera && camera.HasComponent<CameraComponent>())
				return camera.GetComponent<CameraComponent>().ViewProjectionMatrix;
		}

		return m_Camera.GetViewProjectionMatrix();
	}

	void EditorContext::Update(Timestep ts)
	{
		if (!m_Scene)
			return;

		if (m_IsPlaying)
		{
			if (m_IsPaused)
			{
				// A paused frame is normally editor-only, but an explicit
				// Step() request must actually advance the simulation, otherwise
				// single-stepping silently does nothing.
				if (m_Scene->HasPendingSteps())
					m_Scene->OnUpdateSimulation(ts);
				else
					m_Scene->OnUpdateEditor(ts);
			}
			else
			{
				m_Scene->OnUpdateRuntime(ts);
			}
		}
		else
		{
			m_Scene->OnUpdateEditor(ts);
		}
	}

	Ref<Project> EditorContext::NewProject(const std::string& name)
	{
		ProjectConfig config;
		config.Name = name;
		return Project::New(std::move(config));
	}

	bool EditorContext::OpenProject(const std::string& path)
	{
		Ref<Project> project = Project::Load(path);
		if (!project)
			return false;

		// The project names the scene the editor opens. Opening the project
		// without opening its scene leaves the editor looking at whatever
		// was on screen before, which is not what opening a project means.
		//
		// The project is the operation, though: it either loads or it does
		// not. A project whose scene is missing is a project that is open
		// with nothing in it, and saying it failed to open would be a lie
		// about the thing that did not happen.
		const std::string scene = Project::GetEditorScenePath().string();
		if (!scene.empty() && !OpenScene(scene))
		{
			FG_CORE_WARN("OpenProject: the project is open, but its scene '{0}' could not be opened", scene);
		}

		return true;
	}

	bool EditorContext::SaveProject(const std::string& path) const
	{
		Ref<Project> project = Project::GetActive();
		if (!project)
		{
			FG_ERROR("SaveProject: no project is open");
			return false;
		}

		return Project::Save(project, path);
	}

	const ProjectConfig* EditorContext::GetProjectConfig() const
	{
		Ref<Project> project = Project::GetActive();
		return project ? &project->GetConfig() : nullptr;
	}

	std::string EditorContext::ResolveProjectPath(const std::string& relative) const
	{
		if (relative.empty())
			return {};

		const std::filesystem::path resolved = Project::GetAssetFileSystemPath(relative);

		// With no project open the path is its own answer, so the same call
		// works before and after a project is created.
		if (resolved.empty())
			return relative;

		return resolved.string();
	}

	Entity EditorContext::InstantiatePrefab(const std::string& prefabName, bool usePosition, const glm::vec3& position)
	{
		Ref<Prefab> prefab = PrefabLibrary::Get(prefabName);
		if (!prefab)
			return {};

		return prefab->Instantiate(m_Scene.get(), usePosition, position);
	}

	void EditorContext::RecordEntityStateChange(Entity entity, const std::string& before, const std::string& description)
	{
		if (!entity)
			return;

		const std::string after = SceneSerializer::SerializeEntityToString(entity);
		if (after.empty() || after == before)
			return;  // nothing changed: there is nothing to undo

		auto command = std::make_unique<EntityStateCommand>(m_Scene.get(), entity.GetUUID(), before, after,
			[this](Entity target, const std::string& component) { return RemoveComponentQuietly(target, component); });
		command->SetDescription(description);
		m_UndoStack.Push(std::move(command));
	}

	bool EditorContext::SetNativeScript(Entity entity, const std::string& name)
	{
		if (!entity)
			return false;

		BeginEntityEdit(entity);
		entity.AddOrReplaceComponent<NativeScriptComponent>().ClassName = name;
		CommitEntityEdit();

		return true;
	}

	std::string EditorContext::GetNativeScript(Entity entity) const
	{
		if (!entity || !entity.HasComponent<NativeScriptComponent>())
			return std::string();

		return entity.GetComponent<NativeScriptComponent>().ClassName;
	}

	void EditorContext::BeginEntityEdit(Entity entity)
	{
		m_EditingEntity = entity;
		m_EditStateBefore = entity ? SceneSerializer::SerializeEntityToString(entity) : std::string();
		m_EditStateAfter.clear();
	}

	void EditorContext::CommitEntityEdit()
	{
		if (!m_EditingEntity)
		{
			m_EditStateBefore.clear();
			return;
		}

		Entity edited = m_EditingEntity;
		const std::string before = m_EditStateBefore;

		m_EditingEntity = {};
		m_EditStateBefore.clear();

		// The entity may have been destroyed by the edit itself.
		Entity found = m_Scene->GetEntityByUUID(edited.GetUUID());
		if (!found)
			return;

		RecordEntityStateChange(found, before, "Edit Entity");
	}

	void EditorContext::RecordTransformChange(Entity entity, const glm::vec3& beforeTranslation,
		const glm::vec3& beforeRotation, const glm::vec3& beforeScale)
	{
		if (!entity || !entity.HasComponent<TransformComponent>())
			return;

		auto command = std::make_unique<TransformCommand>(m_Scene.get(), entity.GetUUID(),
			beforeTranslation, beforeRotation, beforeScale);
		m_UndoStack.Push(std::move(command));
	}

	bool EditorContext::Undo()
	{
		if (!m_UndoStack.CanUndo())
			return false;

		// The selection is an entity handle, and an undo can replace the
		// entity it points at. The identity is kept, so the selection
		// follows the entity it was on.
		const UUID selected = m_SelectedEntity ? m_SelectedEntity.GetUUID() : UUID(0);

		(void)m_UndoStack.Undo();

		if (selected != UUID(0))
			m_SelectedEntity = m_Scene->GetEntityByUUID(selected);

		return true;
	}

	bool EditorContext::Redo()
	{
		if (!m_UndoStack.CanRedo())
			return false;

		const UUID selected = m_SelectedEntity ? m_SelectedEntity.GetUUID() : UUID(0);

		(void)m_UndoStack.Redo();

		if (selected != UUID(0))
			m_SelectedEntity = m_Scene->GetEntityByUUID(selected);

		return true;
	}

	std::vector<std::string> EditorContext::GetRegisteredComponentTypes() const
	{
		std::vector<std::string> names;
		for (const RuntimeTypeInfo& info : TypeRegistry::GetRegisteredTypes())
			names.push_back(info.Name);
		return names;
	}

	bool EditorContext::AddComponentByName(Entity entity, const std::string& name)
	{
		if (!entity)
		{
			FG_CORE_ERROR("EditorContext::AddComponentByName: entity is not valid");
			return false;
		}

		// The registry is the gate: a name it does not know is not a component,
		// and the switch below only exists for names it does know.
		if (!TypeRegistry::FindType(name))
		{
			FG_CORE_ERROR("EditorContext::AddComponentByName: '{0}' is not a registered component type", name);
			return false;
		}

		// The state before the change, so the undo can take the component
		// away again.
		const std::string before = SceneSerializer::SerializeEntityToString(entity);

#define FG_ADD_COMPONENT_BY_NAME(Comp) \
		if (name == #Comp) \
		{ \
			if (entity.HasComponent<Comp>()) \
				return false; \
			entity.AddComponent<Comp>(); \
			RecordEntityStateChange(entity, before, "Add " + std::string(#Comp)); \
			return true; \
		}

		FG_ADD_COMPONENT_BY_NAME(IDComponent)
		FG_ADD_COMPONENT_BY_NAME(TagComponent)
		FG_ADD_COMPONENT_BY_NAME(TransformComponent)
		FG_ADD_COMPONENT_BY_NAME(CameraComponent)
		FG_ADD_COMPONENT_BY_NAME(ScriptComponent)
		FG_ADD_COMPONENT_BY_NAME(NativeScriptComponent)
		FG_ADD_COMPONENT_BY_NAME(RigidbodyComponent)
		FG_ADD_COMPONENT_BY_NAME(BoxColliderComponent)
		FG_ADD_COMPONENT_BY_NAME(SphereColliderComponent)
		FG_ADD_COMPONENT_BY_NAME(MeshComponent)
		FG_ADD_COMPONENT_BY_NAME(PBRMaterialComponent)
		FG_ADD_COMPONENT_BY_NAME(LightComponent)
		FG_ADD_COMPONENT_BY_NAME(EnvironmentMapComponent)
		FG_ADD_COMPONENT_BY_NAME(AudioSourceComponent)

#undef FG_ADD_COMPONENT_BY_NAME

		// Registered under this build by something that is not an engine
		// component (a test type, a game type): name-addressable, not
		// addable. Saying so beats the alternative of adding nothing and
		// reporting success.
		FG_CORE_WARN("EditorContext::AddComponentByName: '{0}' is registered but not an engine component", name);
		return false;
	}

	bool EditorContext::RemoveComponentByName(Entity entity, const std::string& name)
	{
		if (!entity)
		{
			FG_CORE_ERROR("EditorContext::RemoveComponentByName: entity is not valid");
			return false;
		}

		if (!TypeRegistry::FindType(name))
		{
			FG_CORE_ERROR("EditorContext::RemoveComponentByName: '{0}' is not a registered component type", name);
			return false;
		}

		// Structural components: the engine reads Tag/ID/Transform
		// unconditionally (GetName, GetUUID, the gizmo), so an entity without
		// one is in a state nothing else handles. Never removable.
		if (name == "IDComponent" || name == "TagComponent" || name == "TransformComponent")
		{
			FG_CORE_WARN("EditorContext::RemoveComponentByName: '{0}' is structural and cannot be removed", name);
			return false;
		}

		// The state before the change, so the undo can put the component
		// back - with the values it had.
		const std::string before = SceneSerializer::SerializeEntityToString(entity);

#define FG_REMOVE_COMPONENT_BY_NAME(Comp) \
		if (name == #Comp) \
		{ \
			if (!entity.HasComponent<Comp>()) \
				return false; \
			entity.RemoveComponent<Comp>(); \
			RecordEntityStateChange(entity, before, "Remove " + std::string(#Comp)); \
			return true; \
		}

		FG_REMOVE_COMPONENT_BY_NAME(CameraComponent)
		FG_REMOVE_COMPONENT_BY_NAME(ScriptComponent)
		FG_REMOVE_COMPONENT_BY_NAME(NativeScriptComponent)
		FG_REMOVE_COMPONENT_BY_NAME(RigidbodyComponent)
		FG_REMOVE_COMPONENT_BY_NAME(BoxColliderComponent)
		FG_REMOVE_COMPONENT_BY_NAME(SphereColliderComponent)
		FG_REMOVE_COMPONENT_BY_NAME(MeshComponent)
		FG_REMOVE_COMPONENT_BY_NAME(PBRMaterialComponent)
		FG_REMOVE_COMPONENT_BY_NAME(LightComponent)
		FG_REMOVE_COMPONENT_BY_NAME(EnvironmentMapComponent)
		FG_REMOVE_COMPONENT_BY_NAME(AudioSourceComponent)

#undef FG_REMOVE_COMPONENT_BY_NAME

		FG_CORE_WARN("EditorContext::RemoveComponentByName: '{0}' is registered but not a removable engine component", name);
		return false;
	}

	bool EditorContext::RemoveComponentQuietly(Entity entity, const std::string& name)
	{
		// The removal without a command of its own: the caller is a command
		// that is restoring a state, and a command that pushed another
		// command would undo itself.
		if (!entity)
			return false;

#define FG_QUIETLY_REMOVE_COMPONENT(Comp) \
		if (name == #Comp) \
		{ \
			if (!entity.HasComponent<Comp>()) \
				return false; \
			entity.RemoveComponent<Comp>(); \
			return true; \
		}

		FG_QUIETLY_REMOVE_COMPONENT(CameraComponent)
		FG_QUIETLY_REMOVE_COMPONENT(ScriptComponent)
		FG_QUIETLY_REMOVE_COMPONENT(NativeScriptComponent)
		FG_QUIETLY_REMOVE_COMPONENT(RigidbodyComponent)
		FG_QUIETLY_REMOVE_COMPONENT(BoxColliderComponent)
		FG_QUIETLY_REMOVE_COMPONENT(SphereColliderComponent)
		FG_QUIETLY_REMOVE_COMPONENT(MeshComponent)
		FG_QUIETLY_REMOVE_COMPONENT(PBRMaterialComponent)
		FG_QUIETLY_REMOVE_COMPONENT(LightComponent)
		FG_QUIETLY_REMOVE_COMPONENT(EnvironmentMapComponent)
		FG_QUIETLY_REMOVE_COMPONENT(AudioSourceComponent)

#undef FG_QUIETLY_REMOVE_COMPONENT

		return false;
	}

}
