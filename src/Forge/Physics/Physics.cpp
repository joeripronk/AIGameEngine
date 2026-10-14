#include "fgpch.h"
#include "Forge/Physics/Physics.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"

#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace Forge {

	glm::vec3 Physics::s_Gravity = { 0.0f, -9.81f, 0.0f };
	int Physics::s_StepFrames = 0;
	std::vector<PhysicsBody*> Physics::s_Bodies;

	// PhysicsBody implementation
	PhysicsBody::PhysicsBody()
		: ID(0), Mass(0.0f), InverseMass(0.0f), Restitution(0.0f), Friction(0.5f),
		  IsStatic(true), FixedRotation(false), IsSleeping(false), Shape(ShapeType::Box)
	{
		Position = glm::vec3(0.0f);
		Velocity = glm::vec3(0.0f);
		Acceleration = glm::vec3(0.0f);
		AngularVelocity = glm::vec3(0.0f);
		AngularAcceleration = glm::vec3(0.0f);
		BoxSize = glm::vec3(1.0f);
		SphereRadius = 0.5f;
		Offset = glm::vec3(0.0f);
	}

	PhysicsBody::PhysicsBody(uint64_t id)
		: ID(id), Mass(0.0f), InverseMass(0.0f), Restitution(0.0f), Friction(0.5f),
		  IsStatic(true), FixedRotation(false), IsSleeping(false), Shape(ShapeType::Box)
	{
		Position = glm::vec3(0.0f);
		Velocity = glm::vec3(0.0f);
		Acceleration = glm::vec3(0.0f);
		AngularVelocity = glm::vec3(0.0f);
		AngularAcceleration = glm::vec3(0.0f);
		BoxSize = glm::vec3(1.0f);
		SphereRadius = 0.5f;
		Offset = glm::vec3(0.0f);
	}

	PhysicsBody::PhysicsBody(uint64_t id, const RigidbodyComponent& rb, const BoxColliderComponent& collider, float gravity)
		: ID(id), Mass(collider.Density * collider.Size.x * collider.Size.y * collider.Size.z),
		  InverseMass(0.0f), Restitution(collider.Restitution), Friction(collider.Friction),
		  IsStatic(rb.Type == RigidbodyComponent::BodyType::Static),
		  FixedRotation(rb.FixedRotation), IsSleeping(false), Shape(ShapeType::Box)
	{
		BoxSize = collider.Size;
		SphereRadius = 0.0f;
		Offset = collider.Offset;
		Acceleration = glm::vec3(0.0f, gravity, 0.0f);
		Velocity = glm::vec3(0.0f);
		AngularVelocity = glm::vec3(0.0f);
		AngularAcceleration = glm::vec3(0.0f);
		Position = glm::vec3(0.0f);

		if (IsStatic)
		{
			InverseMass = 0.0f;
		}
		else
		{
			InverseMass = 1.0f / Mass;
		}
	}

	PhysicsBody::PhysicsBody(uint64_t id, const RigidbodyComponent& rb, const SphereColliderComponent& collider, float gravity)
		: ID(id), Mass(collider.Density * (4.0f / 3.0f) * 3.14159f * collider.Radius * collider.Radius * collider.Radius),
		  InverseMass(0.0f), Restitution(collider.Restitution), Friction(collider.Friction),
		  IsStatic(rb.Type == RigidbodyComponent::BodyType::Static),
		  FixedRotation(rb.FixedRotation), IsSleeping(false), Shape(ShapeType::Sphere)
	{
		BoxSize = glm::vec3(0.0f);
		SphereRadius = collider.Radius;
		Offset = collider.Offset;
		Acceleration = glm::vec3(0.0f, gravity, 0.0f);
		Velocity = glm::vec3(0.0f);
		AngularVelocity = glm::vec3(0.0f);
		AngularAcceleration = glm::vec3(0.0f);
		Position = glm::vec3(0.0f);

		if (IsStatic)
		{
			InverseMass = 0.0f;
		}
		else
		{
			InverseMass = 1.0f / Mass;
		}
	}

	glm::vec3 PhysicsBody::GetAABBBottomLeft() const
	{
		if (Shape == ShapeType::Box)
		{
			return Position + Offset - BoxSize * 0.5f;
		}
		// For spheres, use bounding box
		return Position + Offset - glm::vec3(SphereRadius);
	}

	glm::vec3 PhysicsBody::GetAABBBottomRight() const
	{
		if (Shape == ShapeType::Box)
		{
			return Position + Offset + BoxSize * 0.5f;
		}
		return Position + Offset + glm::vec3(SphereRadius);
	}

	glm::vec3 PhysicsBody::GetAABPTopLeft() const
	{
		if (Shape == ShapeType::Box)
		{
			return Position + Offset - BoxSize * 0.5f;
		}
		return Position + Offset - glm::vec3(SphereRadius);
	}

	glm::vec3 PhysicsBody::GetAABPTopRight() const
	{
		if (Shape == ShapeType::Box)
		{
			return Position + Offset + BoxSize * 0.5f;
		}
		return Position + Offset + glm::vec3(SphereRadius);
	}

	bool PhysicsBody::IntersectsAABB(const PhysicsBody& other) const
	{
		glm::vec3 minA = GetAABBBottomLeft();
		glm::vec3 maxA = GetAABBBottomRight();
		glm::vec3 minB = other.GetAABBBottomLeft();
		glm::vec3 maxB = other.GetAABBBottomRight();

		return (minA.x <= maxB.x && maxA.x >= minB.x) &&
			   (minA.y <= maxB.y && maxA.y >= minB.y) &&
			   (minA.z <= maxB.z && maxA.z >= minB.z);
	}

	bool PhysicsBody::IntersectsSphere(const PhysicsBody& other) const
	{
		glm::vec3 centerA = Position + Offset;
		glm::vec3 centerB = other.Position + other.Offset;
		float combinedRadius = SphereRadius + other.SphereRadius;
		float distanceSquared = glm::dot(centerA - centerB, centerA - centerB);
		return distanceSquared <= combinedRadius * combinedRadius;
	}

	glm::mat4 PhysicsBody::GetTransform() const
	{
		glm::mat4 rotation = glm::mat4(1.0f);
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), Position);
		return translation * rotation;
	}

	// Physics engine implementation
	void Physics::Init()
	{
		// Release any bodies left over from a previous session before resetting.
		DestroyBodies();
		s_Gravity = glm::vec3(0.0f, -9.81f, 0.0f);
		s_StepFrames = 0;
		FG_CORE_INFO("Physics initialized (gravity: {0} m/s²)", s_Gravity.y);
	}

	void Physics::Shutdown()
	{
		DestroyBodies();
		FG_CORE_INFO("Physics shut down");
	}

	void Physics::AddBody(PhysicsBody* body)
	{
		s_Bodies.push_back(body);
	}

	void Physics::RemoveBody(PhysicsBody* body)
	{
		auto it = std::find(s_Bodies.begin(), s_Bodies.end(), body);
		if (it != s_Bodies.end())
		{
			delete *it;
			s_Bodies.erase(it);
		}
	}

	void Physics::OnSceneStart()
	{
		CreateBodies();
		FG_CORE_TRACE("Physics scene start: {0} bodies created", s_Bodies.size());
	}

	void Physics::OnSceneStop()
	{
		DestroyBodies();
		FG_CORE_TRACE("Physics scene stop");
	}

	void Physics::Step(int frames)
	{
		s_StepFrames = frames;
	}

	void Physics::Update(float timestep)
	{
		if (s_Bodies.empty())
			return;

		// Apply gravity to dynamic bodies
		for (auto* body : s_Bodies)
		{
			if (!body->IsStatic)
			{
				body->Velocity.y += s_Gravity.y * timestep;
			}
		}

		// Detect collisions
		DetectCollisions();

		// Resolve collisions
		ResolveCollisions(timestep);

		// Integrate bodies
		IntegrateBodies(timestep);

		// Update transform components in scene
		UpdateBodyTransforms();
	}

	void Physics::CreateBodies()
	{
		DestroyBodies();

		// Get scene from Application singleton
		// We'll use a different approach: the Scene will call this with its entity list
		FG_CORE_TRACE("Physics::CreateBodies called (stub - bodies populated via Scene)");
	}

	void Physics::DestroyBodies()
	{
		for (auto* body : s_Bodies)
			delete body;
		s_Bodies.clear();
	}

	void Physics::IntegrateBodies(float timestep)
	{
		for (auto* body : s_Bodies)
		{
			if (body->IsStatic)
				continue;

			// Update velocity
			body->Velocity += body->Acceleration * timestep;

			// Clamp velocity to prevent tunneling
			float maxVelocity = 100.0f;
			float speed = glm::length(body->Velocity);
			if (speed > maxVelocity)
			{
				body->Velocity = glm::normalize(body->Velocity) * maxVelocity;
			}

			// Update position
			body->Position += body->Velocity * timestep;

			// Update angular velocity
			if (!body->FixedRotation)
			{
				body->AngularVelocity += body->AngularAcceleration * timestep;
			}
		}
	}

	void Physics::DetectCollisions()
	{
		for (size_t i = 0; i < s_Bodies.size(); i++)
		{
			for (size_t j = i + 1; j < s_Bodies.size(); j++)
			{
				PhysicsBody* bodyA = s_Bodies[i];
				PhysicsBody* bodyB = s_Bodies[j];

				// Skip if both are static
				if (bodyA->IsStatic && bodyB->IsStatic)
					continue;

				// Check collision
				if (bodyA->IntersectsAABB(*bodyB))
				{
					// Collision detected - store for resolution
					// For now, we'll resolve immediately in ResolveCollisions
				}
			}
		}
	}

	void Physics::ResolveCollisions(float timestep)
	{
		(void)timestep;
		for (size_t i = 0; i < s_Bodies.size(); i++)
		{
			for (size_t j = i + 1; j < s_Bodies.size(); j++)
			{
				PhysicsBody* bodyA = s_Bodies[i];
				PhysicsBody* bodyB = s_Bodies[j];

				// Skip if both are static
				if (bodyA->IsStatic && bodyB->IsStatic)
					continue;

				// Check AABB overlap
				glm::vec3 minA = bodyA->GetAABBBottomLeft();
				glm::vec3 maxA = bodyA->GetAABBBottomRight();
				glm::vec3 minB = bodyB->GetAABBBottomLeft();
				glm::vec3 maxB = bodyB->GetAABBBottomRight();

				// Check overlap on each axis
				float overlapX = std::min(maxA.x, maxB.x) - std::max(minA.x, minB.x);
				float overlapY = std::min(maxA.y, maxB.y) - std::max(minA.y, minB.y);
				float overlapZ = std::min(maxA.z, maxB.z) - std::max(minA.z, minB.z);

				if (overlapX > 0.0f && overlapY > 0.0f && overlapZ > 0.0f)
				{
					// Collision detected - resolve
					// Find minimum overlap axis
					float minOverlap = overlapX;
					int overlapAxis = 0;

					if (overlapY < minOverlap)
					{
						minOverlap = overlapY;
						overlapAxis = 1;
					}
					if (overlapZ < minOverlap)
					{
						minOverlap = overlapZ;
						overlapAxis = 2;
					}

					// Determine collision normal
					glm::vec3 normal = glm::vec3(0.0f);
					if (overlapAxis == 0)
					{
						normal = (minA.x < minB.x) ? glm::vec3(-1.0f, 0.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
					}
					else if (overlapAxis == 1)
					{
						normal = (minA.y < minB.y) ? glm::vec3(0.0f, -1.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
					}
					else
					{
						normal = (minA.z < minB.z) ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
					}

					// Resolve position (positional correction)
					float correction = minOverlap + 0.01f; // Small epsilon to prevent sinking
					if (!bodyA->IsStatic)
					{
						bodyA->Position -= normal * correction * 0.5f;
					}
					if (!bodyB->IsStatic)
					{
						bodyB->Position += normal * correction * 0.5f;
					}

					// Resolve velocity (impulse-based)
					glm::vec3 relativeVelocity = bodyA->Velocity - bodyB->Velocity;
					float velocityAlongNormal = glm::dot(relativeVelocity, normal);

					// Only resolve if bodies are moving toward each other
					if (velocityAlongNormal > 0.0f)
						continue;

					float e = std::min(bodyA->Restitution, bodyB->Restitution);
					float j = -(1.0f + e) * velocityAlongNormal;
					j /= (bodyA->InverseMass + bodyB->InverseMass);

					glm::vec3 impulse = j * normal;

					if (!bodyA->IsStatic)
					{
						bodyA->Velocity += impulse * bodyA->InverseMass;
					}
					if (!bodyB->IsStatic)
					{
						bodyB->Velocity -= impulse * bodyB->InverseMass;
					}

					// Apply friction
					glm::vec3 tangent = relativeVelocity - normal * velocityAlongNormal;
					float tangentLength = glm::length(tangent);
					if (tangentLength > 0.0f)
					{
						tangent = glm::normalize(tangent);
						float friction = std::sqrt(bodyA->Friction * bodyB->Friction);
						float jF = -glm::dot(relativeVelocity, tangent) * friction;
						jF /= (bodyA->InverseMass + bodyB->InverseMass);

						glm::vec3 frictionImpulse = jF * tangent;

						if (!bodyA->IsStatic)
						{
							bodyA->Velocity += frictionImpulse * bodyA->InverseMass;
						}
						if (!bodyB->IsStatic)
						{
							bodyB->Velocity -= frictionImpulse * bodyB->InverseMass;
						}
					}
				}
			}
		}
	}

	void Physics::UpdateBodyTransforms()
	{
		// This would update the TransformComponent in the scene
		// For now, we just log the body positions
		FG_CORE_TRACE("Physics body positions updated ({0} bodies)", s_Bodies.size());
	}

	bool Physics::Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance, glm::vec3& hitPosition, glm::vec3& hitNormal, uint64_t& hitBodyID)
	{
		float rayLength = glm::length(direction);
		if (rayLength < 1e-6f)
			return false;

		glm::vec3 dir = direction / rayLength;
		float closest = maxDistance;
		bool hit = false;

		for (PhysicsBody* body : s_Bodies)
		{
			if (body->Shape == PhysicsBody::ShapeType::Sphere)
			{
				// Ray-sphere intersection
				glm::vec3 center = body->Position + body->Offset;
				glm::vec3 oc = origin - center;
				float b = glm::dot(oc, dir);
				float c = glm::dot(oc, oc) - body->SphereRadius * body->SphereRadius;
				float discriminant = b * b - c;

				if (discriminant < 0.0f)
					continue;

				float t = -b - std::sqrt(discriminant);
				if (t < 0.0f)
					t = -b + std::sqrt(discriminant);

				if (t >= 0.0f && t < closest)
				{
					closest = t;
					hit = true;
					hitPosition = origin + dir * t;
					hitNormal = glm::normalize(hitPosition - center);
					hitBodyID = body->ID;
				}
			}
			else
			{
				// Ray-AABB intersection (slab method)
				glm::vec3 min = body->GetAABBBottomLeft();
				glm::vec3 max = body->GetAABBBottomRight();

				float tmin = 0.0f;
				float tmax = closest;
				glm::vec3 normal(0.0f);
				bool miss = false;

				for (int axis = 0; axis < 3 && !miss; axis++)
				{
					if (std::abs(dir[axis]) < 1e-6f)
					{
						if (origin[axis] < min[axis] || origin[axis] > max[axis])
							miss = true;
					}
					else
					{
						float invD = 1.0f / dir[axis];
						float t1 = (min[axis] - origin[axis]) * invD;
						float t2 = (max[axis] - origin[axis]) * invD;
						float sign = -1.0f;

						if (t1 > t2)
						{
							std::swap(t1, t2);
							sign = 1.0f;
						}

						if (t1 > tmin)
						{
							tmin = t1;
							normal = glm::vec3(0.0f);
							normal[axis] = sign;
						}

						tmax = std::min(tmax, t2);
						if (tmin > tmax)
							miss = true;
					}
				}

				if (!miss && tmin <= tmax && tmin < closest && tmin >= 0.0f)
				{
					closest = tmin;
					hit = true;
					hitPosition = origin + dir * tmin;
					hitNormal = normal;
					hitBodyID = body->ID;
				}
			}
		}

		return hit;
	}

	bool Physics::SphereCast(const glm::vec3& origin, float radius, const glm::vec3& direction, float maxDistance, glm::vec3& hitPosition, glm::vec3& hitNormal)
	{
		// Approximate a sphere cast by offsetting the ray origin along the
		// direction and expanding the effective query distance.
		uint64_t ignoredBodyID = 0;
		return Raycast(origin, direction, maxDistance + radius, hitPosition, hitNormal, ignoredBodyID);
	}

}
