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
	float Physics::s_SleepVelocityThreshold = 0.05f;
	float Physics::s_SleepTimeThreshold = 0.5f;
	std::vector<Physics::ContactKey> Physics::s_ActiveContacts;
	std::vector<Physics::ContactKey> Physics::s_PreviousContacts;
	std::unordered_map<PhysicsBody*, Physics::CollisionCallback> Physics::s_CollisionCallbacks;

	// PhysicsBody implementation
	PhysicsBody::PhysicsBody()
		: ID(0), Mass(0.0f), InverseMass(0.0f), Restitution(0.0f), Friction(0.5f),
		  IsStatic(true), FixedRotation(false), IsSleeping(false), SleepTime(0.0f), Shape(ShapeType::Box)
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
		  IsStatic(true), FixedRotation(false), IsSleeping(false), SleepTime(0.0f), Shape(ShapeType::Box)
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
		  FixedRotation(rb.FixedRotation), IsSleeping(false), SleepTime(0.0f), Shape(ShapeType::Box)
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
		  FixedRotation(rb.FixedRotation), IsSleeping(false), SleepTime(0.0f), Shape(ShapeType::Sphere)
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
		s_SleepVelocityThreshold = 0.05f;
		s_SleepTimeThreshold = 0.5f;
		s_ActiveContacts.clear();
		s_PreviousContacts.clear();
		s_CollisionCallbacks.clear();
		FG_CORE_INFO("Physics initialized (gravity: {0} m/s²)", s_Gravity.y);
	}

	void Physics::Shutdown()
	{
		DestroyBodies();
		s_CollisionCallbacks.clear();
		FG_CORE_INFO("Physics shut down");
	}

	void Physics::AddBody(PhysicsBody* body)
	{
		s_Bodies.push_back(body);
	}

	void Physics::RemoveBody(PhysicsBody* body)
	{
		// Drop the callback registration before the body is deleted so a stale
		// pointer can never be dereferenced by a later dispatch.
		s_CollisionCallbacks.erase(body);

		auto it = std::find(s_Bodies.begin(), s_Bodies.end(), body);
		if (it != s_Bodies.end())
		{
			delete *it;
			s_Bodies.erase(it);
		}
	}

	void Physics::SetCollisionCallback(PhysicsBody* body, CollisionCallback callback)
	{
		if (!body)
			return;

		if (callback)
			s_CollisionCallbacks[body] = callback;
		else
			s_CollisionCallbacks.erase(body);
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

		// Apply gravity to dynamic bodies (skip sleeping bodies)
		for (auto* body : s_Bodies)
		{
			if (!body->IsStatic && !body->IsSleeping)
			{
				body->Velocity.y += s_Gravity.y * timestep;
			}
		}

		// Detect collisions (populates s_ActiveContacts)
		DetectCollisions();

		// Resolve collisions
		ResolveCollisions(timestep);

		// Integrate bodies
		IntegrateBodies(timestep);

		// Update transform components in scene
		UpdateBodyTransforms();

		// Handle body sleeping
		HandleSleeping(timestep);

		// Fire enter/exit callbacks for contacts that began or ended this step.
		DispatchCollisionCallbacks();
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
		s_ActiveContacts.clear();

		for (size_t i = 0; i < s_Bodies.size(); i++)
		{
			for (size_t j = i + 1; j < s_Bodies.size(); j++)
			{
				PhysicsBody* bodyA = s_Bodies[i];
				PhysicsBody* bodyB = s_Bodies[j];

				// Skip if both are static
				if (bodyA->IsStatic && bodyB->IsStatic)
					continue;

				// Spheres use exact sphere-sphere overlap; everything else falls
				// back to the axis-aligned box test.
				const bool touching = bodyA->Shape == PhysicsBody::ShapeType::Sphere &&
					bodyB->Shape == PhysicsBody::ShapeType::Sphere
					? bodyA->IntersectsSphere(*bodyB)
					: bodyA->IntersectsAABB(*bodyB);

				if (touching)
				{
					// Order the pair so the key is stable regardless of body
					// insertion order into s_Bodies.
					ContactKey key{ bodyA->ID < bodyB->ID ? bodyA->ID : bodyB->ID,
					                bodyA->ID < bodyB->ID ? bodyB->ID : bodyA->ID };
					s_ActiveContacts.push_back(key);
				}
			}
		}
	}

	void Physics::DispatchCollisionCallbacks()
	{
		// Nothing to dispatch if no body registered a callback, but the
		// previous-step baseline still has to advance or every contact would
		// look "new" once callbacks are registered later.
		if (s_CollisionCallbacks.empty())
		{
			s_PreviousContacts = s_ActiveContacts;
			return;
		}

		const auto wasInContact = [](const ContactKey& key)
		{
			return std::find(s_PreviousContacts.begin(), s_PreviousContacts.end(), key)
				!= s_PreviousContacts.end();
		};

		const auto isInContact = [](const ContactKey& key)
		{
			return std::find(s_ActiveContacts.begin(), s_ActiveContacts.end(), key)
				!= s_ActiveContacts.end();
		};

		// Deliver a callback to whichever of the two bodies registered one.
		const auto deliver = [](uint64_t selfID, uint64_t otherID, const glm::vec3& normal, ContactPhase phase)
		{
			PhysicsBody* self = FindBodyByID(selfID);
			PhysicsBody* other = FindBodyByID(otherID);
			if (!self || !other)
				return;

			auto entry = s_CollisionCallbacks.find(self);
			if (entry != s_CollisionCallbacks.end() && entry->second)
				entry->second(*self, *other, normal, phase);
		};

		// Contacts that began this step: fire on both bodies' callbacks.
		for (const ContactKey& key : s_ActiveContacts)
		{
			if (wasInContact(key))
				continue;

			deliver(key.A, key.B, glm::vec3(0.0f, 1.0f, 0.0f), ContactPhase::Begin);
			deliver(key.B, key.A, glm::vec3(0.0f, -1.0f, 0.0f), ContactPhase::Begin);
		}

		// Contacts that ended this step: the normal flips so a handler can tell
		// which body the separation pushed.
		for (const ContactKey& key : s_PreviousContacts)
		{
			if (isInContact(key))
				continue;

			deliver(key.A, key.B, glm::vec3(0.0f, -1.0f, 0.0f), ContactPhase::End);
			deliver(key.B, key.A, glm::vec3(0.0f, 1.0f, 0.0f), ContactPhase::End);
		}

		// This step's contacts become the baseline for the next step.
		// s_ActiveContacts is intentionally left intact so callers can query
		// the contacts resolved during the step that just completed.
		s_PreviousContacts = s_ActiveContacts;
	}

	PhysicsBody* Physics::FindBodyByID(uint64_t id)
	{
		for (PhysicsBody* body : s_Bodies)
		{
			if (body->ID == id)
				return body;
		}
		return nullptr;
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

					// Wake up sleeping bodies on collision
					if (bodyA->IsSleeping)
						bodyA->IsSleeping = false;
					if (bodyB->IsSleeping)
						bodyB->IsSleeping = false;

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

	void Physics::HandleSleeping(float timestep)
	{
		for (auto* body : s_Bodies)
		{
			if (body->IsStatic)
				continue;

			float speed = glm::length(body->Velocity);

			if (!body->IsSleeping)
			{
				// Body is awake - accumulate sleep time if velocity is low
				if (speed < s_SleepVelocityThreshold)
				{
					body->SleepTime += timestep;
					if (body->SleepTime >= s_SleepTimeThreshold)
					{
						body->IsSleeping = true;
						body->Velocity = glm::vec3(0.0f);
						body->AngularVelocity = glm::vec3(0.0f);
						FG_CORE_TRACE("Body {0} sleeping", body->ID);
					}
				}
				else
				{
					body->SleepTime = 0.0f;
				}
			}
		}
	}

}
