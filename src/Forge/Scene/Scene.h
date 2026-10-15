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

		Entity GetPrimaryCameraEntity();

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

		void RenderScene();

	private:
		void SyncPhysicsTransforms();

		// --- Audio ---
		//
		// Audio is optional: if no output device is available the sources are
		// simply inert and nothing else about the simulation changes.

		// Starts every AudioSourceComponent whose PlayOnStart is set.
		void StartAudioSources();

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

		void OnComponentAdded(Entity entity, EnvironmentMapComponent& component);
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

}
