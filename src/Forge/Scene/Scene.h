#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Core/Timestep.h"
#include "Forge/Core/UUID.h"
#include "Forge/Scene/Components.h"
#include "Forge/Physics/Physics.h"

#include <entt/entt.hpp>

#include <vector>

class b2World;

namespace Forge {

	class Entity;
	struct PhysicsBody;

	class Scene
	{
	public:
		Scene();
		~Scene();

		static Ref<Scene> Copy(Ref<Scene> other);

		Entity CreateEntity(const std::string& name = std::string());
		Entity CreateEntityWithUUID(UUID uuid, const std::string& name = std::string());
		void DestroyEntity(Entity& entity);

		void OnRuntimeStart();
		void OnRuntimeStop();

		// Physics
		void OnPhysicsStart();
		void OnPhysicsStop();

		// --- Entity collision events ---
		//
		// Handlers are registered per entity and invoked by the physics step when
		// that entity's body starts or stops touching another body. Registering
		// replaces any previous handler; passing nullptr unregisters.
		//
		// `other` is the entity on the far side of the contact, or a null Entity
		// when the other body has no entity (or was destroyed mid-simulation).
		// `normal` points away from the surface towards `self`.
		using CollisionHandler = void (*)(Entity self, Entity other, const glm::vec3& normal, Physics::ContactPhase phase);

		void SetCollisionHandler(Entity entity, CollisionHandler handler);

		// Simulation
		void OnSimulationStart();
		void OnSimulationStop();

		void OnUpdateRuntime(Timestep ts);

		// Advances the animations on the entities that have them and writes
		// their values onto the entities.
		void UpdateAnimations(Timestep ts);

		// Moves the characters: the game says how fast they walk, and the
		// controller resolves the move against the world's static geometry.
		void UpdateCharacterControllers(Timestep ts);
		void OnUpdateSimulation(Timestep ts);
		void OnUpdateEditor(Timestep ts);
		void OnViewportResize(uint32_t width, uint32_t height);

		Entity DuplicateEntity(Entity entity);

		// Entities in creation order.
		//
		// enTT's views iterate in an unspecified order, which varies with the
		// sequence of additions and removals. Anything that must be reproducible
		// (a saved scene, a script update order, "the first primary camera")
		// needs a defined order, so this sorts by entity handle, which is assigned
		// monotonically on creation and therefore matches authoring order.
		std::vector<Entity> GetEntitiesInOrder() const;

		Entity FindEntityByName(std::string_view name);
		Entity GetEntityByUUID(UUID uuid);

		// The closest entity whose collider the ray hits first, within
		// maxDistance - the picking behind a viewport click, the way Hazel's
		// SceneTree selects an entity from the editor camera's ray.
		//
		// Colliders are tested where physics places them (transform translation
		// plus the collider's offset; physics ignores transform scale for
		// colliders, and picking agrees with physics rather than disagreeing),
		// so an entity is clickable exactly where it collides. Works with or
		// without a running physics session, because it reads the components,
		// not bodies. A null Entity when nothing is hit.
		Entity FindEntityOnRay(const glm::vec3& origin, const glm::vec3& direction, float maxDistance);

		// The camera the renderer, the audio listener and the editor all follow.
		// One function, so those three can never disagree about which camera a
		// scene has. Returns the first camera marked primary in authoring order;
		// invalid when the scene has none.
		Entity GetPrimaryCameraEntity();

		// Imports the model a MeshComponent names, if it has not been imported
		// already. Called when the component is added, and by anything that
		// changes ModelPath afterwards - a script setting a path, an inspector
		// field - so that a late path is as good as one set at creation. Returns
		// false for an entity with no mesh, an empty path, or a path that does
		// not resolve; an already-imported model reports success.
		[[nodiscard]]
		bool ImportMeshData(Entity entity);

		[[nodiscard]]
		std::vector<Entity> GetAllCameraEntities() const;

		// Makes one camera the primary and demotes the rest. A scene with two
		// cameras both flagged primary has no defined answer, and which one won
		// used to depend on iteration order rather than on anything an author
		// asked for. Returns false for an entity without a camera.
		[[nodiscard]]
		bool SetPrimaryCamera(Entity camera);

		bool IsRunning() const { return m_IsRunning; }
		bool IsPaused() const { return m_IsPaused; }

		void SetPaused(bool paused) { m_IsPaused = paused; }

		void Step(int frames = 1);

		// Whether a paused frame was requested via Step() and has not yet been
		// consumed. Callers use this to decide between a simulation update and an
		// editor-only update, so that stepping actually advances the world.
		bool HasPendingSteps() const { return m_StepFrames > 0; }

		template<typename... Components>
		auto GetAllEntitiesWith()
		{
			return m_Registry.view<Components...>();
		}

	private:
		void SyncPhysicsTransforms();

		// Copies every component (except the ID) from one entity to another.
		// Single source of truth for duplication and scene copy, so a component
		// added to the engine cannot be silently left out of one of them.
		void CopyComponentsTo(Entity from, Entity to);

		// --- Audio ---
		//
		// Audio is optional: if no output device is available the sources are
		// simply inert and nothing else about the simulation changes.

		// Starts every AudioSourceComponent whose PlayOnStart is set.
		void StartAudioSources();

		// Points the audio listener at the primary camera, which is what the
		// renderer treats as the viewpoint.
		void UpdateAudioListener();

		// Releases finished one-shot sounds and tracks looping ones.
		void UpdateAudioSources();

		// Stops every sound this scene started.
		void StopAudioSources();

		// Copies TransformComponent translations into the matching physics bodies
		// before the step. Physics otherwise only writes body -> transform, so
		// without this a script or the editor that moves an entity during play
		// would silently fail to move the body.
		void SyncPhysicsBodiesFromTransforms();

		// Creates the PhysicsBody for an entity that has just gained a rigidbody
		// and a collider, but only while the simulation is running. Bodies are
		// normally built in bulk by OnPhysicsStart; this covers entities created
		// or completed during simulation (a prefab spawned from Lua, say), which
		// would otherwise never become physical until the next play.
		void EnsurePhysicsBody(Entity entity);

		// Tears down an entity's physics body, if it has one, and clears the
		// RigidbodyComponent's pointer to it. Used when the entity is destroyed
		// and when a physics-bearing component is removed.
		void ReleasePhysicsBody(Entity entity);

		// Destroys every live native script instance, calling OnDestroy first.
		// Called on runtime stop so instances do not survive the session, and
		// from DestroyEntity for the entity being destroyed.
		void DestroyNativeScriptInstances();

		// Bridges a raw PhysicsBody pair to the owning entities and dispatches to
		// any registered Scene-level handlers. Static for the callback signature;
		// the active Scene is tracked in s_CollisionDispatchScene.
		static void DispatchBodyCollision(PhysicsBody& a, PhysicsBody& b, const glm::vec3& normal, Physics::ContactPhase phase);

		// The Scene currently simulating; set for the duration of a physics step.
		static Scene* s_CollisionDispatchScene;

		// Maps a body pointer to the entity that owns it, valid for one step.
		static std::unordered_map<uint64_t, entt::entity> s_BodyOwnerLookup;

		template<typename T>
		void OnComponentAdded(Entity entity, T& component);

		// Called before a component is erased from the registry.
		//
		// Removal is not simply the absence of an addition: a collider or
		// rigidbody removed mid-simulation owns a live PhysicsBody, and erasing
		// the component without releasing it leaves the world colliding with
		// dimensions that no longer exist.
		//
		// Components that own engine resources declare their specialisation just
		// after this class, at namespace scope. Everything else falls through to
		// the no-op primary, which lives in Entity.h because it needs a complete
		// Entity to be defined.
		template<typename T>
		void OnComponentRemoved(Entity entity, T& component);

		void OnComponentAdded(Entity entity, EnvironmentMapComponent& component);
		const entt::registry& GetRegistry() const { return m_Registry; }
		entt::registry m_Registry;
		uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
		bool m_IsRunning = false;
		bool m_IsPaused = false;

		// True between OnPhysicsStart and OnPhysicsStop. Distinct from m_IsRunning
		// because physics can be driven directly, without the full runtime.
		bool m_PhysicsActive = false;
		int m_StepFrames = 0;

		void* m_PhysicsWorld = nullptr;

		std::unordered_map<UUID, entt::entity> m_EntityMap;
		std::unordered_map<entt::entity, PhysicsBody*> m_PhysicsBodies;

		// Collision handlers keyed by entity UUID so they survive across
		// OnPhysicsStart/OnPhysicsStop, which destroy and rebuild bodies.
		std::unordered_map<UUID, CollisionHandler> m_CollisionHandlers;

		friend class Entity;
		friend class SceneSerializer;
	};

	// Removal hooks for the components that own a physics body. Declared here,
	// at namespace scope, so they are visible before any RemoveComponent<T>
	// instantiates the primary template: a use seen before the declaration would
	// implicitly instantiate the no-op instead, leaving the specialisation as a
	// different body under the same name.
	template<>
	void Scene::OnComponentRemoved(Entity entity, RigidbodyComponent& component);

	template<>
	void Scene::OnComponentRemoved(Entity entity, BoxColliderComponent& component);

	template<>
	void Scene::OnComponentRemoved(Entity entity, SphereColliderComponent& component);

	// Addition hooks. Every component is declared, not just the ones that need
	// something, because AddComponent<T> is instantiated from every translation
	// unit that touches a component. A component with no declaration resolved to
	// the primary template, whose definition lives in a .cpp - so it produced an
	// undefined reference at link time instead of a diagnostic at the point of
	// use. NativeScriptComponent was missing one, making the documented native
	// scripting path unusable.
	template<>
	void Scene::OnComponentAdded(Entity entity, IDComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, TagComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, TransformComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, CameraComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, ScriptComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, NativeScriptComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, RigidbodyComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, BoxColliderComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, SphereColliderComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, MeshComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, MultiMeshComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, PBRMaterialComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, LightComponent& component);

	template<>
	void Scene::OnComponentAdded(Entity entity, AudioSourceComponent& component);

}
