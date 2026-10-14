#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Scene/Components.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

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

	private:
		static void CreateBodies();
		static void DestroyBodies();
		static void UpdateBodyTransforms();
		static void DetectCollisions();
		static void ResolveCollisions(float timestep);
		static void IntegrateBodies(float timestep);
		static void HandleSleeping(float timestep);

		static std::vector<PhysicsBody*> s_Bodies;
		static glm::vec3 s_Gravity;
		static int s_StepFrames;
		static float s_SleepVelocityThreshold;
		static float s_SleepTimeThreshold;
	};

	// Represents a physics body in the simulation
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
