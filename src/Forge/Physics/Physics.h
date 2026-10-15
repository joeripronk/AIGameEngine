#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Scene/Components.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <unordered_map>
#include <vector>

namespace Forge {

	class Scene;
	class Entity;

	struct PhysicsBody;

	class Physics
	{
	public:
		static void Init();
		static void Shutdown();

		static void OnSceneStart();
		static void OnSceneStop();

		static void Update(float timestep);
		static void Step(int frames = 1);

		// Collision queries
		static bool Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, glm::vec3& hitPosition, glm::vec3& hitNormal, uint64_t& hitBodyID);
		static bool SphereCast(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance, glm::vec3& hitPosition, glm::vec3& hitNormal);

		// Accessors
		static float GetGravityY() { return s_Gravity.y; }
		static void SetGravityY(float gravityY) { s_Gravity = glm::vec3(0.0f, gravityY, 0.0f); }
		static glm::vec3 GetGravity() { return s_Gravity; }
		static void SetGravity(glm::vec3 gravity) { s_Gravity = gravity; }
		static const std::vector<PhysicsBody*>& GetBodies() { return s_Bodies; }

		// Body management
		static void AddBody(PhysicsBody* body);
		static void RemoveBody(PhysicsBody* body);

		// Sleep thresholds (configurable)
		static float GetSleepVelocityThreshold() { return s_SleepVelocityThreshold; }
		static void SetSleepVelocityThreshold(float threshold) { s_SleepVelocityThreshold = threshold; }
		static float GetSleepTimeThreshold() { return s_SleepTimeThreshold; }
		static void SetSleepTimeThreshold(float threshold) { s_SleepTimeThreshold = threshold; }

		// --- Collision callbacks ---
		//
		// Callbacks fire once per contact pair: on the step where the contact
		// begins (ContactPhase::Begin) and on the step where it stops
		// (ContactPhase::End). A pair that is still touching on the next step
		// fires neither, so Begin/End are balanced edge events rather than a
		// per-step "still colliding" stream.
		//
		// Bodies with no registered callback simply do not receive events, so
		// static geometry can stay silent while dynamic bodies react.
		enum class ContactPhase { Begin, End };

		// `normal` points from B towards A for Begin, and from A towards B for End.
		using CollisionCallback = void (*)(PhysicsBody& a, PhysicsBody& b, const glm::vec3& normal, ContactPhase phase);

		struct ContactKey
		{
			uint64_t A;
			uint64_t B;

			// Contact keys are stored in a hash set, so define equality and a
			// stable hash. Bodies are always registered with A < B.
			bool operator==(const ContactKey& other) const { return A == other.A && B == other.B; }
		};

		struct ContactKeyHash
		{
			size_t operator()(const ContactKey& key) const
			{
				// FNV-1a over the two ids.
				size_t hash = 14695981039346656037ull;
				hash ^= static_cast<size_t>(key.A);
				hash *= 1099511628211ull;
				hash ^= static_cast<size_t>(key.B);
				hash *= 1099511628211ull;
				return hash;
			}
		};

		// Registers a callback for one body. Passing nullptr unregisters.
		static void SetCollisionCallback(PhysicsBody* body, CollisionCallback callback);

		// The contacts currently being resolved this step (for debugging/tests).
		static const std::vector<ContactKey>& GetActiveContacts() { return s_ActiveContacts; }

	private:
		static void CreateBodies();
		static void DestroyBodies();
		static void UpdateBodyTransforms();
		static void DetectCollisions();
		static void ResolveCollisions(float timestep);
		static void IntegrateBodies(float timestep);
		static void HandleSleeping(float timestep);

		// Fires enter/exit callbacks for contacts that began or ended this step.
		static void DispatchCollisionCallbacks();

		// Resolves a body ID back to its live PhysicsBody, or nullptr if the body
		// was removed from the world.
		static PhysicsBody* FindBodyByID(uint64_t id);

		static std::vector<PhysicsBody*> s_Bodies;
		static glm::vec3 s_Gravity;
		static int s_StepFrames;
		static float s_SleepVelocityThreshold;
		static float s_SleepTimeThreshold;

		// Contacts that overlapped during the current step, and the contacts that
		// overlapped during the previous step (for enter/exit detection).
		static std::vector<ContactKey> s_ActiveContacts;
		static std::vector<ContactKey> s_PreviousContacts;

		// Per-body callback dispatch table.
		static std::unordered_map<PhysicsBody*, CollisionCallback> s_CollisionCallbacks;
	};

	// Represents a physics body in the simulation.
	//
	// A body is exactly one of:
	//   Static    - never moves; infinite mass; no collision response on it.
	//   Kinematic  - moved only by its own Velocity, ignores forces and gravity,
	//               has infinite mass so dynamic bodies collide with it without
	//               pushing it. Use for platforms, elevators and moving hazards.
	//   Dynamic   - fully simulated: gravity, forces, collision response.
	struct PhysicsBody
	{
		uint64_t ID;
		glm::vec3 Position;
		glm::vec3 Velocity;
		glm::vec3 Acceleration;
		glm::vec3 AngularVelocity;
		glm::vec3 AngularAcceleration;
		float Mass;
		float InverseMass;
		float Restitution;
		float Friction;
		bool IsStatic;
		bool IsKinematic;
		bool FixedRotation;
		bool IsSleeping;
		float SleepTime; // Accumulated sleep time (seconds)

		// Collision shape
		enum class ShapeType { Box, Sphere };
		ShapeType Shape;
		glm::vec3 BoxSize; // For box shapes
		float SphereRadius; // For sphere shapes
		glm::vec3 Offset; // Offset from entity transform

		PhysicsBody();
		PhysicsBody(uint64_t id);
		PhysicsBody(uint64_t id, const RigidbodyComponent& rb, const BoxColliderComponent& collider, float gravity);
		PhysicsBody(uint64_t id, const RigidbodyComponent& rb, const SphereColliderComponent& collider, float gravity);

		glm::vec3 GetAABBBottomLeft() const;
		glm::vec3 GetAABBBottomRight() const;
		glm::vec3 GetAABPTopLeft() const;
		glm::vec3 GetAABPTopRight() const;

		bool IntersectsAABB(const PhysicsBody& other) const;
		bool IntersectsSphere(const PhysicsBody& other) const;

		glm::mat4 GetTransform() const;
	};

}
