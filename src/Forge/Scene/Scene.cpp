#include "fgpch.h"
#include "Forge/Debug/Instrumentor.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"

#include "Forge/Scene/Components.h"
#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Scripting/ScriptableEntity.h"
#include "Forge/Scripting/ScriptEngine.h"
#include "Forge/Physics/Physics.h"
#include "Forge/Audio/AudioEngine.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Forge/Renderer/Renderer.h"
#include "Forge/Renderer/Mesh.h"

#endif // FG_NO_VULKAN_RUNTIME

namespace Forge {

	// Only one Scene simulates at a time, so a single static pointer is enough
	// to bridge the physics callback back to entity handlers.
	Scene* Scene::s_CollisionDispatchScene = nullptr;
	std::unordered_map<uint64_t, entt::entity> Scene::s_BodyOwnerLookup;

	Scene::Scene()
	{
		// Every consumer of components builds a scene, so this is the place
		// the component registry is guaranteed to be populated - by use, not
		// by hoping a static initialiser survives the static-library link.
		RegisterEngineComponentTypes();
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
		// A native script holds a reference to its entity and owns a live
		// instance. Destroying the instance first lets it observe OnDestroy while
		// the handle is still valid; erasing the component first would hand it a
		// dangling reference.
		if (auto* nsc = m_Registry.try_get<NativeScriptComponent>((entt::entity)entity))
		{
			if (nsc->Instance)
			{
				nsc->Instance->OnDestroy();

				if (nsc->DestroyScript)
					nsc->DestroyScript(nsc);

				nsc->Instance = nullptr;
			}
		}

		// If the entity owns a physics body, release it first.
		ReleasePhysicsBody(entity);

		m_EntityMap.erase(entity.GetUUID());
		m_Registry.destroy(entity);
		// Invalidate the entity handle so it evaluates to false
		entity.m_EntityHandle = entt::null;
	}

	void Scene::DestroyNativeScriptInstances()
	{
		for (auto e : m_Registry.view<NativeScriptComponent>())
		{
			auto* nscPtr = m_Registry.try_get<NativeScriptComponent>(e);
			if (!nscPtr || !nscPtr->Instance)
				continue;

			auto& nsc = *nscPtr;

			nsc.Instance->OnDestroy();

			if (nsc.DestroyScript)
				nsc.DestroyScript(&nsc);

			// Cleared, not just destroyed: the component survives a stop/start
			// cycle, and a stale Instance would be dereferenced on the next run.
			nsc.Instance = nullptr;
		}
	}

	void Scene::ReleasePhysicsBody(Entity entity)
	{
		auto bodyIt = m_PhysicsBodies.find((entt::entity)entity);
		if (bodyIt == m_PhysicsBodies.end())
			return;

		PhysicsBody* body = bodyIt->second;

		// Read while the body is still alive. Physics::RemoveBody frees it, so
		// reading body->ID afterwards is a use-after-free - which is exactly what
		// this did, and which only AddressSanitizer caught: Debug reused the
		// freed block and read back a stale-but-plausible id, so the lookup
		// entry silently leaked instead of failing.
		const uint64_t bodyId = body->ID;

		// The body's RigidbodyComponent points at it, and that pointer has to
		// be cleared too: leaving it dangling means the next EnsurePhysicsBody
		// finds a non-null RuntimeBody for an entity whose body is gone.
		if (auto* rb = m_Registry.try_get<RigidbodyComponent>((entt::entity)entity))
			rb->RuntimeBody = nullptr;

		// Cleared before the body leaves the world, so nothing can resolve it to
		// an owner while it is still mid-removal.
		s_BodyOwnerLookup.erase(bodyId);

		Physics::RemoveBody(body);
		m_PhysicsBodies.erase(bodyIt);
	}

	void Scene::OnRuntimeStart()
	{
		m_IsRunning = true;

		OnPhysicsStart();
		StartAudioSources();

		// Scripting: ScriptEngine loads each entity's script and calls OnCreate.
		ScriptEngine::OnRuntimeStart(this);
	}

	void Scene::OnRuntimeStop()
	{
		m_IsRunning = false;

		StopAudioSources();
		OnPhysicsStop();

		ScriptEngine::OnRuntimeStop();

		// Native instances are destroyed after the Lua ones so a native script
		// can still see a stopped-but-intact scene on the way out.
		DestroyNativeScriptInstances();
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
		FG_PROFILE_FUNCTION();
		if (!m_IsPaused || m_StepFrames-- > 0)
		{
			// Reclaim finished sounds before the rest of the frame, so a one-shot
			// that ended during the previous frame does not hold its handle.
			UpdateAudioSources();

			// Update scripts
			{
				// C# Entity OnUpdate
				auto view = m_Registry.view<ScriptComponent>();
				for (auto e : view)
				{
					Entity entity = { e, this };
					ScriptEngine::OnUpdateEntity(entity, ts);
				}

				const auto nscView = m_Registry.view<NativeScriptComponent>();
				for (auto e : nscView)
				{
					auto& nsc = std::get<0>(nscView.get(e));

					if (!nsc.Instance)
					{
						// An unbound component has no factory, and calling through a
						// null function pointer would jump to nothing. An unbound
						// native script is inert rather than fatal, matching an
						// AudioSource with no clip.
						if (!nsc.InstantiateScript)
							continue;

						nsc.Instance = nsc.InstantiateScript();
						if (!nsc.Instance)
							continue;

						// Copied into the instance rather than pointed at: the wrapper
						// built here is a temporary of this loop, so a pointer to it
						// would dangle before the next update.
						Entity nscOwner = { e, this };
						nsc.Instance->SetEntity(nscOwner);
						nsc.Instance->OnCreate();
					}

					nsc.Instance->OnUpdate(ts);
				}
			}

			// Physics
			{
				// Publish this Scene for the duration of the step so the static
				// collision bridge can resolve bodies back to entities.
				s_CollisionDispatchScene = this;

				// Pull any transform edits made since the last step into the
				// bodies before integrating.
				SyncPhysicsBodiesFromTransforms();
				Physics::Update(ts);

				s_CollisionDispatchScene = nullptr;

				// Write simulation results back into the scene transforms
				SyncPhysicsTransforms();
			}
		}

		// Rendering is not the scene's job: the application owns the frame and
		// calls Renderer::RenderScene. Doing it here re-opened the command list
		// the application had already opened, closed it, and then recorded the
		// real frame into a closed list.
	}

	void Scene::OnUpdateSimulation(Timestep ts)
	{
		FG_PROFILE_FUNCTION();
		(void)ts;
		if (!m_IsPaused || m_StepFrames-- > 0)
		{
			// Physics
			{
				s_CollisionDispatchScene = this;
				SyncPhysicsBodiesFromTransforms();
				Physics::Update(ts);
				s_CollisionDispatchScene = nullptr;
				SyncPhysicsTransforms();
			}
		}

	}

	void Scene::OnUpdateEditor(Timestep ts)
	{
		(void)ts;
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

	bool Scene::ImportMeshData(Entity entity)
	{
		if (!entity || !entity.HasComponent<MeshComponent>())
		{
			FG_CORE_ERROR("ImportMeshData: entity has no MeshComponent");
			return false;
		}

		MeshComponent& component = entity.GetComponent<MeshComponent>();

		// An empty path is not a failure: the entity has geometry supplied some
		// other way, or none yet.
		if (component.ModelPath.empty())
			return false;

		if (component.CpuMeshData)
			return true;

		component.CpuMeshData = CpuMesh::LoadFromFile(component.ModelPath);
		if (!component.CpuMeshData)
		{
			FG_CORE_ERROR("Entity '{0}': could not import model '{1}'", entity.GetName(), component.ModelPath);
			return false;
		}

		// A new import invalidates whatever was uploaded from the old one.
		component.Mesh = nullptr;

		FG_CORE_INFO("Entity '{0}': imported '{1}' ({2} vertices)",
			entity.GetName(), component.ModelPath, component.CpuMeshData->GetVertices().size());
		return true;
	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		// Authoring order, not registry iteration order: with two cameras marked
		// primary the answer has to be the same every run.
		for (Entity entity : GetEntitiesInOrder())
		{
			if (entity.HasComponent<CameraComponent>() && entity.GetComponent<CameraComponent>().Primary)
				return entity;
		}
		return {};
	}

	std::vector<Entity> Scene::GetAllCameraEntities() const
	{
		std::vector<Entity> cameras;
		for (Entity entity : GetEntitiesInOrder())
		{
			if (entity.HasComponent<CameraComponent>())
				cameras.push_back(entity);
		}
		return cameras;
	}

	bool Scene::SetPrimaryCamera(Entity camera)
	{
		if (!camera || !camera.HasComponent<CameraComponent>())
		{
			FG_CORE_ERROR("SetPrimaryCamera: entity has no CameraComponent");
			return false;
		}

		for (Entity other : GetAllCameraEntities())
			other.GetComponent<CameraComponent>().Primary = (other == camera);

		return true;
	}

	void Scene::Step(int frames)
	{
		m_StepFrames = frames;
	}

	std::vector<Entity> Scene::GetEntitiesInOrder() const
	{
		std::vector<Entity> entities;

		auto view = m_Registry.view<IDComponent>();

		for (auto handle : view)
			entities.push_back(Entity{ handle, const_cast<Scene*>(this) });

		// Entity handles are assigned monotonically on creation, so sorting by
		// handle yields authoring order.
		std::sort(entities.begin(), entities.end(),
			[](const Entity& a, const Entity& b)
			{
				return static_cast<uint32_t>(a) < static_cast<uint32_t>(b);
			});

		return entities;
	}

	// One list of copied components, shared by duplication and scene copy.
	//
	// These used to be two hand-maintained lists, and they had already drifted:
	// EnvironmentMapComponent, AudioSourceComponent and NativeScriptComponent
	// were in neither, so duplicating an entity silently dropped its environment
	// probe, its sound and its native behaviour. Two lists cannot both be right
	// for as long as adding a component means remembering two edits; one cannot
	// drift at all, because it is the only list.
	template<typename T>
	void CopyComponentIfPresent(Entity from, Entity to)
	{
		if (from.HasComponent<T>())
			to.AddOrReplaceComponent<T>(from.GetComponent<T>());
	}

	void Scene::CopyComponentsTo(Entity from, Entity to)
	{
		CopyComponentIfPresent<TagComponent>(from, to);
		CopyComponentIfPresent<TransformComponent>(from, to);
		CopyComponentIfPresent<CameraComponent>(from, to);
		CopyComponentIfPresent<ScriptComponent>(from, to);
		CopyComponentIfPresent<RigidbodyComponent>(from, to);
		CopyComponentIfPresent<BoxColliderComponent>(from, to);
		CopyComponentIfPresent<SphereColliderComponent>(from, to);
		CopyComponentIfPresent<LightComponent>(from, to);
		CopyComponentIfPresent<PBRMaterialComponent>(from, to);
		CopyComponentIfPresent<MeshComponent>(from, to);
		CopyComponentIfPresent<EnvironmentMapComponent>(from, to);
		CopyComponentIfPresent<AudioSourceComponent>(from, to);

		// The native binding is copied, not the live instance: two entities
		// sharing one ScriptableEntity would share its state, and the original
		// keeps the instance it was already running.
		if (from.HasComponent<NativeScriptComponent>())
		{
			const NativeScriptComponent& original = from.GetComponent<NativeScriptComponent>();
			NativeScriptComponent& copy = to.AddOrReplaceComponent<NativeScriptComponent>();
			copy.InstantiateScript = original.InstantiateScript;
			copy.DestroyScript = original.DestroyScript;
		}

		// IDComponent is deliberately not copied: the copy is a new entity and
		// keeps the UUID CreateEntity assigned it.
	}

	Entity Scene::DuplicateEntity(Entity entity)
	{
		FG_CORE_ASSERT_MSG(entity, "Cannot duplicate an invalid entity!");

		// A fresh entity with a new UUID, and every component the original had.
		Entity newEntity = CreateEntity(entity.GetName());
		this->CopyComponentsTo(entity, newEntity);

		return newEntity;
	}

	Ref<Scene> Scene::Copy(Ref<Scene> other)
	{
		if (!other)
		{
			FG_CORE_ERROR("Scene::Copy: no scene to copy");
			return nullptr;
		}

		auto copy = CreateRef<Scene>();

		auto view = other->m_Registry.view<IDComponent>();
		for (auto entityID : view)
		{
			Entity originalEntity{ entityID, other.get() };
			if (!originalEntity)
				continue;

			// The UUID is preserved, so a copy can be saved and reopened as the
			// same scene rather than a new one.
			const auto id = originalEntity.GetComponent<IDComponent>().ID;
			Entity newEntity = copy->CreateEntityWithUUID(id, originalEntity.GetName());

			copy->CopyComponentsTo(originalEntity, newEntity);
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

	namespace {

		// Ray entry distance against an axis-aligned box (slab method), or
		// -1 when the ray misses. An origin inside the box hits at 0.
		float RayBoxEntryDistance(const glm::vec3& origin, const glm::vec3& invDirection,
			const glm::vec3& min, const glm::vec3& max)
		{
			float tMin = 0.0f;
			float tMax = 1e30f;
			for (int axis = 0; axis < 3; ++axis)
			{
				const float t0 = (min[axis] - origin[axis]) * invDirection[axis];
				const float t1 = (max[axis] - origin[axis]) * invDirection[axis];
				tMin = glm::max(tMin, glm::min(t0, t1));
				tMax = glm::min(tMax, glm::max(t0, t1));
				if (tMin > tMax)
					return -1.0f;
			}
			return tMin;
		}

		// Ray entry distance against a sphere, or -1 when the ray misses.
		float RaySphereEntryDistance(const glm::vec3& origin, const glm::vec3& direction,
			const glm::vec3& center, float radius)
		{
			const glm::vec3 m = center - origin;
			const float b = glm::dot(m, direction);
			const float c = glm::dot(m, m) - radius * radius;

			// The sphere is behind the ray and the ray points away from it.
			if (c > 0.0f && b < 0.0f)
				return -1.0f;

			const float discriminant = b * b - c;
			if (discriminant < 0.0f)
				return -1.0f;

			// The nearer root; a negative root means the origin is inside.
			return glm::max(0.0f, b - glm::sqrt(discriminant));
		}

	}

	Entity Scene::FindEntityOnRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance)
	{
		const glm::vec3 dir = glm::normalize(direction);
		if (glm::length2(dir) < 0.5f || maxDistance <= 0.0f)
			return {};

		// Division by zero for a ray parallel to a slab is the point of the
		// inverse direction: ±inf makes those slabs unconstrained, which is
		// exactly the geometry.
		const glm::vec3 invDirection(1.0f / dir.x, 1.0f / dir.y, 1.0f / dir.z);

		Entity best;
		float bestDistance = maxDistance;

		auto boxView = m_Registry.view<BoxColliderComponent, TransformComponent>();
		for (auto handle : boxView)
		{
			const auto& collider = boxView.get<BoxColliderComponent>(handle);
			const auto& transform = boxView.get<TransformComponent>(handle);

			const glm::vec3 center = transform.Translation + collider.Offset;
			const glm::vec3 half = collider.Size * 0.5f;

			const float distance = RayBoxEntryDistance(origin, invDirection, center - half, center + half);
			if (distance >= 0.0f && distance <= bestDistance)
			{
				bestDistance = distance;
				best = Entity{ handle, this };
			}
		}

		auto sphereView = m_Registry.view<SphereColliderComponent, TransformComponent>();
		for (auto handle : sphereView)
		{
			const auto& collider = sphereView.get<SphereColliderComponent>(handle);
			const auto& transform = sphereView.get<TransformComponent>(handle);

			const float distance = RaySphereEntryDistance(origin, dir, transform.Translation + collider.Offset, collider.Radius);
			if (distance >= 0.0f && distance <= bestDistance)
			{
				bestDistance = distance;
				best = Entity{ handle, this };
			}
		}

		return best;
	}

	void Scene::OnPhysicsStart()
	{
		// Initialize physics world and create bodies from scene components
		Physics::Init();
		Physics::OnSceneStart();

		// Set before the bodies are built, so EnsurePhysicsBody is consistent
		// whether a body comes from the bulk pass or a later addition.
		m_PhysicsActive = true;

		m_PhysicsBodies.clear();

		// Bodies are addressed by entity handle for the lifetime of the step;
		// the collision dispatcher uses this to recover the owning entities.
		s_BodyOwnerLookup.clear();

		// A body whose entity has a registered handler gets the Scene bridge
		// callback, so Physics never needs to know about entities itself.
		const auto attachHandler = [this](entt::entity handle)
		{
			auto it = m_Registry.try_get<IDComponent>(handle);
			if (!it)
				return;

			auto handler = m_CollisionHandlers.find(it->ID);
			if (handler != m_CollisionHandlers.end() && handler->second)
				Physics::SetCollisionCallback(m_PhysicsBodies[handle], &Scene::DispatchBodyCollision);
		};

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
			s_BodyOwnerLookup[body->ID] = e;
			attachHandler(e);
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
			s_BodyOwnerLookup[body->ID] = e;
			attachHandler(e);
		}

		FG_CORE_INFO("Physics started with {0} bodies", Physics::GetBodies().size());
	}

	void Scene::SetCollisionHandler(Entity entity, CollisionHandler handler)
	{
		if (!entity)
			return;

		const UUID uuid = entity.GetUUID();

		if (!handler)
		{
			m_CollisionHandlers.erase(uuid);

			// Also detach from the live body if the simulation is running.
			auto bodyIt = m_PhysicsBodies.find((entt::entity)entity);
			if (bodyIt != m_PhysicsBodies.end())
				Physics::SetCollisionCallback(bodyIt->second, nullptr);
			return;
		}

		m_CollisionHandlers[uuid] = handler;

		// If the body already exists, bind immediately so callers do not have to
		// register handlers only before playback.
		auto bodyIt = m_PhysicsBodies.find((entt::entity)entity);
		if (bodyIt != m_PhysicsBodies.end())
			Physics::SetCollisionCallback(bodyIt->second, &Scene::DispatchBodyCollision);
	}

	void Scene::DispatchBodyCollision(PhysicsBody& a, PhysicsBody& b, const glm::vec3& normal, Physics::ContactPhase phase)
	{
		Scene* scene = s_CollisionDispatchScene;
		if (!scene)
			return;

		// Resolve a body back to its entity; a body with no entity (or one that
		// was destroyed mid-simulation) yields a null Entity rather than
		// aborting dispatch.
		const auto owner = [scene](PhysicsBody& body) -> Entity
		{
			auto it = s_BodyOwnerLookup.find(body.ID);
			if (it == s_BodyOwnerLookup.end() || !scene->m_Registry.valid(it->second))
				return Entity{ entt::null, scene };

			return Entity{ it->second, scene };
		};

		Entity self = owner(a);
		Entity other = owner(b);

		if (!self)
			return;

		auto it = scene->m_CollisionHandlers.find(self.GetUUID());
		if (it == scene->m_CollisionHandlers.end() || !it->second)
			return;

		// Physics already oriented `normal` to point away from the surface
		// towards `self` for this side of the pair, so it forwards unchanged.
		it->second(self, other, normal, phase);
	}

	void Scene::OnPhysicsStop()
	{
		// Clear runtime body pointers on the components before the bodies die.
		auto view = m_Registry.view<RigidbodyComponent>();
		for (auto e : view)
			view.get<RigidbodyComponent>(e).RuntimeBody = nullptr;

		m_PhysicsBodies.clear();

		// Body pointers are gone, so the body->entity lookup must not outlive
		// them. Handlers themselves are kept so they still apply on the next run.
		s_BodyOwnerLookup.clear();
		s_CollisionDispatchScene = nullptr;
		m_PhysicsActive = false;

		Physics::OnSceneStop();
		Physics::Shutdown();
	}

	void Scene::EnsurePhysicsBody(Entity entity)
	{
		// Outside an active physics world there is nothing to join; OnPhysicsStart
		// builds every body up front. Keyed on the physics lifecycle rather than
		// the broader runtime flag, so a caller driving physics directly still
		// gets correct behaviour.
		if (!m_PhysicsActive || !entity)
			return;

		// Already has one, or not physical yet: a rigidbody needs a collider
		// before it has a shape.
		if (m_PhysicsBodies.find((entt::entity)entity) != m_PhysicsBodies.end())
			return;

		auto* rb = m_Registry.try_get<RigidbodyComponent>((entt::entity)entity);
		auto* transform = m_Registry.try_get<TransformComponent>((entt::entity)entity);
		if (!rb || !transform)
			return;

		PhysicsBody* body = nullptr;

		if (auto* box = m_Registry.try_get<BoxColliderComponent>((entt::entity)entity))
			body = new PhysicsBody(static_cast<uint64_t>(static_cast<entt::entity>(entity)), *rb, *box, Physics::GetGravityY());
		else if (auto* sphere = m_Registry.try_get<SphereColliderComponent>((entt::entity)entity))
			body = new PhysicsBody(static_cast<uint64_t>(static_cast<entt::entity>(entity)), *rb, *sphere, Physics::GetGravityY());

		if (!body)
			return;

		body->Position = transform->Translation;

		rb->RuntimeBody = body;
		m_PhysicsBodies[(entt::entity)entity] = body;
		s_BodyOwnerLookup[body->ID] = (entt::entity)entity;
		Physics::AddBody(body);

		// A scripted entity spawned mid-simulation still expects contacts.
		if (m_Registry.try_get<ScriptComponent>((entt::entity)entity) && !m_CollisionHandlers.empty())
		{
			auto it = m_CollisionHandlers.find(entity.GetUUID());
			if (it != m_CollisionHandlers.end() && it->second)
				Physics::SetCollisionCallback(body, &Scene::DispatchBodyCollision);
		}
	}

	void Scene::StartAudioSources()
	{
		if (!AudioEngine::Init())
		{
			// Reported once by AudioEngine; do not treat audio as fatal.
			FG_CORE_INFO("Scene: continuing without audio");
			return;
		}

		UpdateAudioListener();

		auto view = m_Registry.view<AudioSourceComponent>();
		for (auto handle : view)
		{
			auto& audio = view.get<AudioSourceComponent>(handle);

			// An empty path is inert by design, so a half-authored entity does
			// not spam warnings every run.
			if (!audio.PlayOnStart || audio.SourcePath.empty())
				continue;

			AudioEngine::SpatialParams spatial;
			spatial.Enabled = audio.Spatial;
			spatial.MinDistance = audio.MinDistance;
			spatial.MaxDistance = audio.MaxDistance;

			const glm::vec3* position = nullptr;
			glm::vec3 worldPosition(0.0f);
			if (audio.Spatial)
			{
				if (const auto* transform = m_Registry.try_get<TransformComponent>(handle))
					worldPosition = transform->Translation;

				position = &worldPosition;
			}

			AudioEngine::SoundHandle sound = AudioEngine::InvalidSound;
			// The bus is passed at start rather than set afterwards, so the very
			// first sample is mixed through the right bus.
			const bool started = position
				? AudioEngine::PlaySoundAtOnBus(audio.SourcePath, *position, spatial, audio.Bus,
					audio.Volume, audio.Pitch, audio.Loop, &sound)
				: AudioEngine::PlaySoundOnBus(audio.SourcePath, audio.Bus,
					audio.Volume, audio.Pitch, audio.Loop, &sound);

			if (!started)
			{
				FG_CORE_WARN("Scene: could not start audio source '{0}'", audio.SourcePath);
				continue;
			}

			audio.SoundHandle = sound;
			audio.Finished = false;
		}
	}

	void Scene::UpdateAudioListener()
	{
		// The primary camera is the listener, matching how it drives rendering.
		Forge::AudioEngine::ListenerState listener;
		listener.Position = { 0.0f, 0.0f, 0.0f };

		Entity camera = GetPrimaryCameraEntity();
		if (camera && camera.HasComponent<TransformComponent>())
		{
			const auto& tc = camera.GetComponent<TransformComponent>();
			listener.Position = tc.Translation;

			// The engine's camera convention is +X right, +Y up, -Z forward.
			const glm::vec3 forward = glm::normalize(tc.Rotation);
			listener.Forward = forward;
			listener.Up = { 0.0f, 1.0f, 0.0f };
		}

		AudioEngine::SetListener(listener);
	}

	void Scene::UpdateAudioSources()
	{
		auto view = m_Registry.view<AudioSourceComponent>();
		for (auto handle : view)
		{
			auto& audio = view.get<AudioSourceComponent>(handle);
			if (audio.SoundHandle == AudioEngine::InvalidSound)
				continue;

			// A spatial source follows its entity, so a sound attached to a moving
			// object pans and attenuates as that object moves.
			if (audio.Spatial)
			{
				if (const auto* transform = m_Registry.try_get<TransformComponent>(handle))
					(void)AudioEngine::SetSoundPosition(audio.SoundHandle, transform->Translation);
			}

			if (AudioEngine::IsSoundPlaying(audio.SoundHandle))
				continue;

			// No longer playing: for a one-shot this is the natural end, so
			// release the handle instead of holding a finished sound for the
			// whole session.
			if (AudioEngine::StopSound(audio.SoundHandle))
				audio.SoundHandle = AudioEngine::InvalidSound;

			audio.Finished = true;
		}

		// Reclaim finished sounds and re-mix spatial audio. Update() does both, so a
		// faded-out sound is released only once its fade has actually finished
		// rather than being cut off mid-decay.
		UpdateAudioListener();
		AudioEngine::Update();
	}

	void Scene::StopAudioSources()
	{
		auto view = m_Registry.view<AudioSourceComponent>();
		for (auto handle : view)
		{
			auto& audio = view.get<AudioSourceComponent>(handle);
			if (audio.SoundHandle == AudioEngine::InvalidSound)
				continue;

			// Handles came from this scene's own PlaySound calls, so this cannot
			// fail; the cast records that.
			(void)AudioEngine::StopSound(audio.SoundHandle);
			audio.SoundHandle = AudioEngine::InvalidSound;
		}
	}

	void Scene::SyncPhysicsBodiesFromTransforms()
	{
		for (auto& [entityHandle, body] : m_PhysicsBodies)
		{
			if (!body || body->IsStatic || !m_Registry.valid(entityHandle))
				continue;

			const auto* transform = m_Registry.try_get<TransformComponent>(entityHandle);
			if (!transform)
				continue;

			body->Position = transform->Translation;
		}
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
		(void)component;

		// An entity that acquires a Rigidbody during simulation needs a body
		// immediately, not at the next OnPhysicsStart.
		EnsurePhysicsBody(entity);
	}

	template<>
	void Scene::OnComponentAdded<BoxColliderComponent>(Entity entity, BoxColliderComponent& component)
	{
		(void)component;
		EnsurePhysicsBody(entity);
	}

	template<>
	void Scene::OnComponentAdded<SphereColliderComponent>(Entity entity, SphereColliderComponent& component)
	{
		(void)component;
		EnsurePhysicsBody(entity);
	}

	template<>
	void Scene::OnComponentRemoved<RigidbodyComponent>(Entity entity, RigidbodyComponent& component)
	{
		(void)component;
		ReleasePhysicsBody(entity);
	}

	template<>
	void Scene::OnComponentRemoved<BoxColliderComponent>(Entity entity, BoxColliderComponent& component)
	{
		(void)component;
		ReleasePhysicsBody(entity);
	}

	template<>
	void Scene::OnComponentRemoved<SphereColliderComponent>(Entity entity, SphereColliderComponent& component)
	{
		(void)component;
		ReleasePhysicsBody(entity);
	}

	template<>
	void Scene::OnComponentAdded<NativeScriptComponent>(Entity entity, NativeScriptComponent& component)
	{
		// Nothing to do at add time: the instance is created on the first update,
		// once physics and the rest of the scene are in a consistent state. The
		// hook exists because AddComponent is instantiated from every translation
		// unit that touches the component, and without a declaration the primary
		// template had no visible definition - a link error, not a no-op.
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
	void Scene::OnComponentAdded<AudioSourceComponent>(Entity entity, AudioSourceComponent& component)
	{
		// Adding the component mid-simulation should start the sound, otherwise
		// a script that attaches a source at runtime would never be heard.
		if (!m_PhysicsActive || !entity)
			return;

		if (!component.PlayOnStart || component.SourcePath.empty())
			return;

		if (component.SoundHandle != AudioEngine::InvalidSound)
			return;   // already playing

		AudioEngine::SoundHandle sound = AudioEngine::InvalidSound;
		if (AudioEngine::PlaySound(component.SourcePath, component.Volume, component.Pitch, component.Loop, &sound))
			component.SoundHandle = sound;
	}

	template<>
	void Scene::OnComponentAdded<MeshComponent>(Entity entity, MeshComponent& component)
	{
		// Import the model the component names. An empty path is legitimate (the
		// entity has geometry supplied some other way, or none yet), but a path
		// that does not resolve is a mistake worth saying out loud: it used to
		// produce a unit cube at run time and nothing else.
		(void)component;
		(void)ImportMeshData(entity);
	}

	void Scene::OnComponentAdded(Entity entity, EnvironmentMapComponent& component)
	{
		(void)entity; (void)component;
	}

}
