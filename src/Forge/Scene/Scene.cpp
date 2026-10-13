#include "fgpch.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"

#include "Forge/Scene/Components.h"
#include "Forge/Scripting/ScriptableEntity.h"
#include "Forge/Scripting/ScriptEngine.h"

namespace Forge {

	Scene::Scene()
	{
	}

	Scene::~Scene()
	{
		if (m_PhysicsWorld)
			delete m_PhysicsWorld;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		Entity entity = { m_Registry.create(), this };
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<TransformComponent>();
		auto& tag = entity.AddComponent<TagComponent>();
		tag.Tag = name.empty() ? "Entity" : name;

		m_EntityMap[uuid] = entity;

		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		m_EntityMap.erase(entity.GetUUID());
		m_Registry.destroy(entity);
	}

	void Scene::OnRuntimeStart()
	{
		m_IsRunning = true;

		OnPhysicsStart();

		// Scripting
		{
			ScriptEngine::OnRuntimeStart(this);
			// Instantiate all script entities

			auto view = m_Registry.view<ScriptComponent>();
			for (auto e : view)
			{
				Entity entity = { e, this };
				ScriptEngine::OnCreateEntity(entity);
			}
		}
	}

	void Scene::OnRuntimeStop()
	{
		m_IsRunning = false;

		OnPhysicsStop();

		ScriptEngine::OnRuntimeStop();
	}

	void Scene::OnSimulationStart()
	{
		OnPhysicsStart();
	}

	void Scene::OnSimulationStop()
	{
		OnPhysicsStop();
	}

	void Scene::OnUpdateRuntime(Timestep ts)
	{
		if (!m_IsPaused || m_StepFrames-- > 0)
		{
			// Update scripts
			{
				// C# Entity OnUpdate
				auto view = m_Registry.view<ScriptComponent>();
				for (auto e : view)
				{
					Entity entity = { e, this };
					ScriptEngine::OnUpdateEntity(entity, ts);
				}

				m_Registry.view<NativeScriptComponent>().each([=](auto entity, auto& nsc)
					{
						if (!nsc.Instance)
						{
							nsc.Instance = nsc.InstantiateScript();
							nsc.Instance->m_Entity = Entity{ entity, this };
							nsc.Instance->OnCreate();
						}

						nsc.Instance->OnUpdate(ts);
					});
			}

			// Physics
			{
				const int32_t velocityIterations = 6;
				const int32_t positionIterations = 2;
				// m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);

				// Retrieve transform from physics
				auto view = m_Registry.view<RigidbodyComponent>();
				for (auto e : view)
				{
					Entity entity = { e, this };
					auto& transform = entity.GetComponent<TransformComponent>();
					auto& rb = entity.GetComponent<RigidbodyComponent>();

					// TODO: Update transform from physics body
				}
			}
		}

		// Render
		RenderScene();
	}

	void Scene::OnUpdateSimulation(Timestep ts)
	{
		if (!m_IsPaused || m_StepFrames-- > 0)
		{
			// Physics
			{
				const int32_t velocityIterations = 6;
				const int32_t positionIterations = 2;
				// m_PhysicsWorld->Step(ts, velocityIterations, positionIterations);

				// Retrieve transform from physics
				auto view = m_Registry.view<RigidbodyComponent>();
				for (auto e : view)
				{
					Entity entity = { e, this };
					auto& transform = entity.GetComponent<TransformComponent>();
					auto& rb = entity.GetComponent<RigidbodyComponent>();

					// TODO: Update transform from physics body
				}
			}
		}

		// Render
		RenderScene();
	}

	void Scene::OnUpdateEditor(Timestep ts)
	{
		// Render
		RenderScene();
	}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		if (m_ViewportWidth == width && m_ViewportHeight == height)
			return;

		m_ViewportWidth = width;
		m_ViewportHeight = height;

		// Resize cameras
		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{
			auto& camera = view.get<CameraComponent>(entity);
			// camera.SetViewportSize(width, height);
		}
	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		auto view = m_Registry.view<CameraComponent>();
		for (auto entity : view)
		{
			const auto& camera = view.get<CameraComponent>(entity);
			if (camera.Primary)
				return Entity{entity, this};
		}
		return {};
	}

	void Scene::Step(int frames)
	{
		m_StepFrames = frames;
	}

	Entity Scene::DuplicateEntity(Entity entity)
	{
		std::string name = entity.GetName();
		Entity newEntity = CreateEntity(name);
		// TODO: Copy all components
		return newEntity;
	}

	Entity Scene::FindEntityByName(std::string_view name)
	{
		auto view = m_Registry.view<TagComponent>();
		for (auto entity : view)
		{
			const TagComponent& tc = view.get<TagComponent>(entity);
			if (tc.Tag == name)
				return Entity{ entity, this };
		}
		return {};
	}

	Entity Scene::GetEntityByUUID(UUID uuid)
	{
		if (m_EntityMap.find(uuid) != m_EntityMap.end())
			return { m_EntityMap.at(uuid), this };

		return {};
	}

	void Scene::OnPhysicsStart()
	{
		// TODO: Initialize physics world
		// m_PhysicsWorld = new b2World({ 0.0f, -9.8f });

		auto view = m_Registry.view<RigidbodyComponent>();
		for (auto e : view)
		{
			Entity entity = { e, this };
			auto& transform = entity.GetComponent<TransformComponent>();
			auto& rb = entity.GetComponent<RigidbodyComponent>();

			// TODO: Create physics body
			// b2BodyDef bodyDef;
			// bodyDef.type = Utils::RigidbodyTypeToBox2DBody(rb.Type);
			// bodyDef.position.Set(transform.Translation.x, transform.Translation.y);
			// bodyDef.angle = transform.Rotation.z;
			// b2Body* body = m_PhysicsWorld->CreateBody(&bodyDef);
			// rb.RuntimeBody = body;
		}
	}

	void Scene::OnPhysicsStop()
	{
		// TODO: Delete physics world
		// delete m_PhysicsWorld;
		// m_PhysicsWorld = nullptr;
	}

	void Scene::RenderScene()
	{
		// TODO: Implement 3D rendering
	}

	template<typename T>
	void Scene::OnComponentAdded(Entity entity, T& component)
	{
		static_assert(sizeof(T) == 0);
	}

	template<>
	void Scene::OnComponentAdded<IDComponent>(Entity entity, IDComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<TransformComponent>(Entity entity, TransformComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component)
	{
		if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
		{
			// component.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
		}
	}

	template<>
	void Scene::OnComponentAdded<ScriptComponent>(Entity entity, ScriptComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<TagComponent>(Entity entity, TagComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<RigidbodyComponent>(Entity entity, RigidbodyComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<BoxColliderComponent>(Entity entity, BoxColliderComponent& component)
	{
	}

	template<>
	void Scene::OnComponentAdded<SphereColliderComponent>(Entity entity, SphereColliderComponent& component)
	{
	}

}
