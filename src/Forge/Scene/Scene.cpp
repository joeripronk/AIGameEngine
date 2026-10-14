#include "fgpch.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"

#include "Forge/Scene/Components.h"
#include "Forge/Scripting/ScriptableEntity.h"
#include "Forge/Scripting/ScriptEngine.h"
#include "Forge/Physics/Physics.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Forge/Renderer/Renderer.h"
#include "Forge/Renderer/Mesh.h"
#include "Forge/Renderer/Shader.h"
#include "Forge/Renderer/ShaderLibrary.h"
#include "Forge/Renderer/VertexArray.h"
#include "Forge/Renderer/VertexBuffer.h"
#include "Forge/Renderer/IndexBuffer.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#endif // FG_NO_VULKAN_RUNTIME

namespace Forge {

	Scene::Scene()
	{
	}

	Scene::~Scene()
	{
		// Physics world is managed externally; no delete here
		m_PhysicsWorld = nullptr;
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

	void Scene::DestroyEntity(Entity& entity)
	{
		// If the entity owns a physics body, release it first.
		auto bodyIt = m_PhysicsBodies.find((entt::entity)entity);
		if (bodyIt != m_PhysicsBodies.end())
		{
			Physics::RemoveBody(bodyIt->second);
			m_PhysicsBodies.erase(bodyIt);
		}

		m_EntityMap.erase(entity.GetUUID());
		m_Registry.destroy(entity);
		// Invalidate the entity handle so it evaluates to false
		entity.m_EntityHandle = entt::null;
	}

	void Scene::OnRuntimeStart()
	{
		m_IsRunning = true;

		OnPhysicsStart();

		// Scripting: ScriptEngine loads each entity's script and calls OnCreate.
		ScriptEngine::OnRuntimeStart(this);
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

				const auto& nscView = m_Registry.view<NativeScriptComponent>();
				for (auto e : nscView)
				{
					auto& nsc = std::get<0>(nscView.get(e));
					if (!nsc.Instance)
					{
						nsc.Instance = nsc.InstantiateScript();
						nsc.Instance->SetEntity(nullptr); // TODO: Fix entity pointer lifecycle
						nsc.Instance->OnCreate();
					}

					nsc.Instance->OnUpdate(ts);
				}
			}

			// Physics
			{
				// Step physics simulation
				Physics::Update(ts);

				// Write simulation results back into the scene transforms
				SyncPhysicsTransforms();
			}
		}

		// Render
#if !defined(FG_NO_VULKAN_RUNTIME)
		RenderScene();
#endif
	}

	void Scene::OnUpdateSimulation(Timestep ts)
	{
		(void)ts;
		if (!m_IsPaused || m_StepFrames-- > 0)
		{
			// Physics
			{
				Physics::Update(ts);
				SyncPhysicsTransforms();
			}
		}

		// Render
#if !defined(FG_NO_VULKAN_RUNTIME)
		RenderScene();
#endif
	}

	void Scene::OnUpdateEditor(Timestep ts)
	{
		(void)ts;
		// Render
#if !defined(FG_NO_VULKAN_RUNTIME)
		RenderScene();
#endif
	}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		if (m_ViewportWidth == width && m_ViewportHeight == height)
			return;

		m_ViewportWidth = width;
		m_ViewportHeight = height;

		if (width == 0 || height == 0)
			return;

		// Rebuild each camera's projection for the new aspect ratio.
		float aspect = (float)width / (float)height;
		auto view = m_Registry.view<CameraComponent>();
		for (auto e : view)
		{
			auto& camera = view.get<CameraComponent>(e);
			camera.AspectRatio = aspect;
			camera.UpdateProjection();
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
		FG_CORE_ASSERT_MSG(entity, "Cannot duplicate an invalid entity!");

		// Create a fresh entity (new UUID) and deep-copy every component.
		Entity newEntity = CreateEntity(entity.GetName());

		if (entity.HasComponent<TagComponent>())
			newEntity.GetComponent<TagComponent>() = entity.GetComponent<TagComponent>();

		if (entity.HasComponent<TransformComponent>())
			newEntity.GetComponent<TransformComponent>() = entity.GetComponent<TransformComponent>();

		if (entity.HasComponent<CameraComponent>())
			newEntity.AddOrReplaceComponent<CameraComponent>(entity.GetComponent<CameraComponent>());

		if (entity.HasComponent<ScriptComponent>())
			newEntity.AddOrReplaceComponent<ScriptComponent>(entity.GetComponent<ScriptComponent>());

		if (entity.HasComponent<RigidbodyComponent>())
			newEntity.AddOrReplaceComponent<RigidbodyComponent>(entity.GetComponent<RigidbodyComponent>());

		if (entity.HasComponent<BoxColliderComponent>())
			newEntity.AddOrReplaceComponent<BoxColliderComponent>(entity.GetComponent<BoxColliderComponent>());

		if (entity.HasComponent<SphereColliderComponent>())
			newEntity.AddOrReplaceComponent<SphereColliderComponent>(entity.GetComponent<SphereColliderComponent>());

		if (entity.HasComponent<LightComponent>())
			newEntity.AddOrReplaceComponent<LightComponent>(entity.GetComponent<LightComponent>());

		if (entity.HasComponent<PBRMaterialComponent>())
			newEntity.AddOrReplaceComponent<PBRMaterialComponent>(entity.GetComponent<PBRMaterialComponent>());

		if (entity.HasComponent<MeshComponent>())
			newEntity.AddOrReplaceComponent<MeshComponent>(entity.GetComponent<MeshComponent>());

		return newEntity;
	}

	Ref<Scene> Scene::Copy(Ref<Scene> other)
	{
		auto copy = CreateRef<Scene>();

		// Copy entities with all components
		auto view = other->m_Registry.view<IDComponent>();
		for (auto entityID : view)
		{
			Entity originalEntity{ entityID, other.get() };
			if (!originalEntity)
				continue;

			// Create entity with preserved UUID. This already adds the
			// IDComponent, TransformComponent and TagComponent, so those are
			// assigned in-place below rather than added again.
			auto id = originalEntity.GetComponent<IDComponent>().ID;
			Entity newEntity = copy->CreateEntityWithUUID(id, originalEntity.GetName());

			// Copy TagComponent (already present, assign in place)
			if (originalEntity.HasComponent<TagComponent>())
			{
				newEntity.GetComponent<TagComponent>() = originalEntity.GetComponent<TagComponent>();
			}

			// Copy TransformComponent (already present, assign in place)
			if (originalEntity.HasComponent<TransformComponent>())
			{
				newEntity.GetComponent<TransformComponent>() = originalEntity.GetComponent<TransformComponent>();
			}

			// Copy CameraComponent
			if (originalEntity.HasComponent<CameraComponent>())
			{
				newEntity.AddComponent<CameraComponent>(originalEntity.GetComponent<CameraComponent>());
			}

			// Copy ScriptComponent
			if (originalEntity.HasComponent<ScriptComponent>())
			{
				newEntity.AddComponent<ScriptComponent>(originalEntity.GetComponent<ScriptComponent>());
			}

			// Copy RigidbodyComponent
			if (originalEntity.HasComponent<RigidbodyComponent>())
			{
				newEntity.AddComponent<RigidbodyComponent>(originalEntity.GetComponent<RigidbodyComponent>());
			}

			// Copy BoxColliderComponent
			if (originalEntity.HasComponent<BoxColliderComponent>())
			{
				newEntity.AddComponent<BoxColliderComponent>(originalEntity.GetComponent<BoxColliderComponent>());
			}

			// Copy SphereColliderComponent
			if (originalEntity.HasComponent<SphereColliderComponent>())
			{
				newEntity.AddComponent<SphereColliderComponent>(originalEntity.GetComponent<SphereColliderComponent>());
			}

			// Copy LightComponent
			if (originalEntity.HasComponent<LightComponent>())
			{
				newEntity.AddComponent<LightComponent>(originalEntity.GetComponent<LightComponent>());
			}

			// Copy PBRMaterialComponent
			if (originalEntity.HasComponent<PBRMaterialComponent>())
			{
				newEntity.AddComponent<PBRMaterialComponent>(originalEntity.GetComponent<PBRMaterialComponent>());
			}

			// Copy MeshComponent
			if (originalEntity.HasComponent<MeshComponent>())
			{
				newEntity.AddComponent<MeshComponent>(originalEntity.GetComponent<MeshComponent>());
			}
		}

		return copy;
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
		// Initialize physics world and create bodies from scene components
		Physics::Init();
		Physics::OnSceneStart();

		m_PhysicsBodies.clear();

		// Box colliders
		auto view = m_Registry.view<RigidbodyComponent, TransformComponent, BoxColliderComponent>();
		for (auto e : view)
		{
			auto& rb = std::get<0>(view.get(e));
			auto& transform = std::get<1>(view.get(e));

			PhysicsBody* body = new PhysicsBody((uint64_t)e, rb, std::get<2>(view.get(e)), Physics::GetGravityY());
			body->Position = transform.Translation;

			rb.RuntimeBody = body;
			m_PhysicsBodies[e] = body;
			Physics::AddBody(body);
		}

		// Sphere colliders
		auto sphereView = m_Registry.view<RigidbodyComponent, TransformComponent, SphereColliderComponent>();
		for (auto e : sphereView)
		{
			auto& rb = std::get<0>(sphereView.get(e));
			auto& transform = std::get<1>(sphereView.get(e));

			PhysicsBody* body = new PhysicsBody((uint64_t)e, rb, std::get<2>(sphereView.get(e)), Physics::GetGravityY());
			body->Position = transform.Translation;

			rb.RuntimeBody = body;
			m_PhysicsBodies[e] = body;
			Physics::AddBody(body);
		}

		FG_CORE_INFO("Physics started with {0} bodies", Physics::GetBodies().size());
	}

	void Scene::OnPhysicsStop()
	{
		// Clear runtime body pointers on the components before the bodies die.
		auto view = m_Registry.view<RigidbodyComponent>();
		for (auto e : view)
			view.get<RigidbodyComponent>(e).RuntimeBody = nullptr;

		m_PhysicsBodies.clear();

		Physics::OnSceneStop();
		Physics::Shutdown();
	}

	void Scene::SyncPhysicsTransforms()
	{
		for (auto& [entityHandle, body] : m_PhysicsBodies)
		{
			if (!body || !m_Registry.valid(entityHandle))
				continue;

			auto& transform = m_Registry.get<TransformComponent>(entityHandle);
			transform.Translation = body->Position;
		}
	}

#if !defined(FG_NO_VULKAN_RUNTIME)

	void Scene::RenderScene()
	{
		// Get primary camera
		auto cameraEntity = GetPrimaryCameraEntity();
		if (!cameraEntity)
			return;

		auto& camera = cameraEntity.GetComponent<CameraComponent>();
		auto& transform = cameraEntity.GetComponent<TransformComponent>();

		// Update camera view matrix from transform. Rotation is stored in
		// radians (see TransformComponent::GetTransform).
		glm::mat4 rotation = glm::toMat4(glm::quat(transform.Rotation));
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.Translation);

		camera.ViewMatrix = glm::inverse(translation * rotation);
		camera.ViewProjectionMatrix = camera.ProjectionMatrix * camera.ViewMatrix;

		// Begin scene
		Renderer::BeginScene();

		// Set view projection matrix in shader
		{
			auto pbrShader = Renderer::GetShaderLibrary().Get("PBR");
			if (pbrShader)
			{
				pbrShader->SetMat4("u_ViewProjection", camera.ViewProjectionMatrix);
			}
		}

		// Render all mesh entities
		auto meshView = m_Registry.view<TransformComponent, MeshComponent>();
		for (auto entity : meshView)
		{
			auto& transformComp = meshView.get<TransformComponent>(entity);
			auto& meshComp = meshView.get<MeshComponent>(entity);

			if (!meshComp.Mesh)
			{
				// Load mesh from file
				meshComp.Mesh = Mesh::Create(meshComp.ModelPath);
			}

			if (meshComp.Mesh)
			{
				Renderer::Submit(
					Renderer::GetShaderLibrary().Get("PBR"),
					meshComp.Mesh->GetVertexArray(),
					transformComp.GetTransform()
				);
			}
		}

		// Render entities with PBR material but no mesh (use default cube)
		auto materialView = m_Registry.view<TransformComponent, PBRMaterialComponent>();
		for (auto entity : materialView)
		{
			// TODO: Render default cube with PBR material
			(void)entity;
		}

		// End scene
		Renderer::EndScene();
	}

#endif // FG_NO_VULKAN_RUNTIME

	template<typename T>
	void Scene::OnComponentAdded(Entity entity, T& component)
	{
		static_assert(sizeof(T) == 0);
	}

	template<>
	void Scene::OnComponentAdded<IDComponent>(Entity entity, IDComponent& component)
	{
		(void)entity; (void)component;
	}

	template<>
	void Scene::OnComponentAdded<TransformComponent>(Entity entity, TransformComponent& component)
	{
		(void)entity; (void)component;
	}

	template<>
	void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component)
	{
		(void)entity; (void)component;
		if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
		{
			// component.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
		}
	}

	template<>
	void Scene::OnComponentAdded<ScriptComponent>(Entity entity, ScriptComponent& component)
	{
		(void)entity; (void)component;
	}

	template<>
	void Scene::OnComponentAdded<TagComponent>(Entity entity, TagComponent& component)
	{
		(void)entity; (void)component;
	}

	template<>
	void Scene::OnComponentAdded<RigidbodyComponent>(Entity entity, RigidbodyComponent& component)
	{
		(void)entity; (void)component;
	}

	template<>
	void Scene::OnComponentAdded<BoxColliderComponent>(Entity entity, BoxColliderComponent& component)
	{
		(void)entity; (void)component;
	}

	template<>
	void Scene::OnComponentAdded<SphereColliderComponent>(Entity entity, SphereColliderComponent& component)
	{
		(void)entity; (void)component;
	}

	template<>
	void Scene::OnComponentAdded<LightComponent>(Entity entity, LightComponent& component)
	{
		(void)entity; (void)component;
	}

	template<>
	void Scene::OnComponentAdded<PBRMaterialComponent>(Entity entity, PBRMaterialComponent& component)
	{
		(void)entity; (void)component;
	}

	template<>
	void Scene::OnComponentAdded<MeshComponent>(Entity entity, MeshComponent& component)
	{
		(void)entity; (void)component;
	}

	void Scene::OnComponentAdded(Entity entity, EnvironmentMapComponent& component)
	{
		(void)entity; (void)component;
	}

}
