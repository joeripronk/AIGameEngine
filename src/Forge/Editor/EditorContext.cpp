#include "fgpch.h"
#include "Forge/Editor/EditorContext.h"

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Scene/Prefab.h"
#include "Forge/Physics/Physics.h"

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

		SceneSerializer serializer(m_Scene);
		serializer.Serialize(path);
		return true;
	}

	void EditorContext::SelectEntity(Entity entity)
	{
		m_SelectedEntity = entity;
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
			m_IsPaused = false;
			m_Scene->SetPaused(false);
			return;
		}

		m_IsPlaying = true;
		m_IsPaused = false;

		// The Scene holds its own pause flag and OnUpdateRuntime reads it, so
		// the context and the scene must be told together. Pause() may have been
		// called before playback ever started.
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

	Entity EditorContext::InstantiatePrefab(const std::string& prefabName, bool usePosition, const glm::vec3& position)
	{
		Ref<Prefab> prefab = PrefabLibrary::Get(prefabName);
		if (!prefab)
			return {};

		return prefab->Instantiate(m_Scene.get(), usePosition, position);
	}

}
