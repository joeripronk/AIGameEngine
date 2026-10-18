#include "fgpch.h"
#include "Forge/Editor/EditorContext.h"

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
		return true;
	}

	bool EditorContext::SaveScene(const std::string& path) const
	{
		if (!m_Scene)
			return false;

		// Serialize reports failure; forwarding it means an agent that saves a
		// scene learns whether the save actually happened.
		SceneSerializer serializer(m_Scene);
		return serializer.Serialize(path);
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
		return m_Scene->CreateEntity(name);
	}

	void EditorContext::DestroyEntity(Entity entity)
	{
		// Only a real destroy can invalidate the selection. Passing an already
		// invalid entity must be a no-op, not a way to silently clear selection.
		if (!entity)
			return;

		if (m_SelectedEntity == entity)
			m_SelectedEntity = {};

		m_Scene->DestroyEntity(entity);
	}

	Entity EditorContext::DuplicateEntity(Entity entity)
	{
		return m_Scene->DuplicateEntity(entity);
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
		return Project::Load(path) != nullptr;
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

#define FG_ADD_COMPONENT_BY_NAME(Comp) \
		if (name == #Comp) \
		{ \
			if (entity.HasComponent<Comp>()) \
				return false; \
			entity.AddComponent<Comp>(); \
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

#define FG_REMOVE_COMPONENT_BY_NAME(Comp) \
		if (name == #Comp) \
		{ \
			if (!entity.HasComponent<Comp>()) \
				return false; \
			entity.RemoveComponent<Comp>(); \
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

}
