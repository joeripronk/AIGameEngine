#include "fgpch.h"
#include "Forge/Physics/Physics.h"
#include "Forge/Debug/Instrumentor.h"
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
	bool Physics::s_ContinuousCollision = false;
	uint32_t Physics::s_MaxSubsteps = 8;
	uint32_t Physics::s_LastSubstepCount = 1;
	std::vector<Physics::ContactKey> Physics::s_ActiveContacts;
	std::vector<Physics::ContactKey> Physics::s_PreviousContacts;
	std::unordered_map<PhysicsBody*, Physics::CollisionCallback> Physics::s_CollisionCallbacks;

	// PhysicsBody implementation
	PhysicsBody::PhysicsBody()
		: ID(0), Mass(0.0f), InverseMass(0.0f), Restitution(0.0f), Friction(0.5f),
		  IsStatic(true), IsKinematic(false), FixedRotation(false), IsSleeping(false), SleepTime(0.0f), Shape(ShapeType::Box)
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
		  IsStatic(true), IsKinematic(false), FixedRotation(false), IsSleeping(false), SleepTime(0.0f), Shape(ShapeType::Box)
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
		  IsKinematic(rb.Type == RigidbodyComponent::BodyType::Kinematic),
		  FixedRotation(rb.FixedRotation), IsSleeping(false), SleepTime(0.0f), Shape(ShapeType::Box),
		  CollisionLayer(rb.CollisionLayer), CollisionMask(rb.CollisionMask)
	{
		BoxSize = collider.Size;
		SphereRadius = 0.0f;
		Offset = collider.Offset;
		Acceleration = glm::vec3(0.0f, gravity, 0.0f);
		Velocity = glm::vec3(0.0f);
		AngularVelocity = glm::vec3(0.0f);
		AngularAcceleration = glm::vec3(0.0f);
		Position = glm::vec3(0.0f);

		// Kinematic bodies behave as if infinitely massive so dynamic bodies
		// collide with them without pushing them.
		if (IsStatic || IsKinematic)
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
		  IsKinematic(rb.Type == RigidbodyComponent::BodyType::Kinematic),
		  FixedRotation(rb.FixedRotation), IsSleeping(false), SleepTime(0.0f), Shape(ShapeType::Sphere),
		  CollisionLayer(rb.CollisionLayer), CollisionMask(rb.CollisionMask)
	{
		BoxSize = glm::vec3(0.0f);
		SphereRadius = collider.Radius;
		Offset = collider.Offset;
		Acceleration = glm::vec3(0.0f, gravity, 0.0f);
		Velocity = glm::vec3(0.0f);
		AngularVelocity = glm::vec3(0.0f);
		AngularAcceleration = glm::vec3(0.0f);
		Position = glm::vec3(0.0f);

		// Kinematic bodies behave as if infinitely massive so dynamic bodies
		// collide with them without pushing them.
		if (IsStatic || IsKinematic)
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

		// Reset solver configuration too. Init() is the start of a session, so
		// leaving these set would let one run's tuning silently leak into the
		// next (and, in tests, across test cases).
		s_ContinuousCollision = false;
		s_MaxSubsteps = 8;
		s_LastSubstepCount = 1;

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
		FG_PROFILE_FUNCTION();
		if (s_Bodies.empty())
			return;

		// Contacts are accumulated across every substep, so a contact that is
		// detected and resolved on substep 2 of 8 still counts as having
		// happened this frame.
		s_ActiveContacts.clear();

		// Without CCD a single iteration is the whole step.
		if (!s_ContinuousCollision)
		{
			s_LastSubstepCount = 1;
			SimulateSubstep(timestep);
			UpdateBodyTransforms();
			DispatchCollisionCallbacks();
			return;
		}

		// Adaptive substepping: split the frame so no body can travel far enough
		// in one substep to pass through the thinnest collider in the scene.
		const uint32_t substeps = ComputeSubstepCount(timestep);
		s_LastSubstepCount = substeps;

		const float substepTime = timestep / static_cast<float>(substeps);
		for (uint32_t i = 0; i < substeps; i++)
		{
			SimulateSubstep(substepTime);

			// Once everything has come to rest the remaining substeps cannot
			// change anything, so stop early. Note this tests for rest directly:
			// asking ComputeSubstepCount again would be circular, since by
			// construction the current substep is short enough not to need
			// subdividing.
			if (i + 1 < substeps && IsWorldAtRest())
				break;
		}

		UpdateBodyTransforms();
		DispatchCollisionCallbacks();
	}

	bool Physics::IsWorldAtRest()
	{
		for (PhysicsBody* body : s_Bodies)
		{
			// Static bodies never move. Kinematic bodies are driven by their own
			// velocity, so a fast platform is not at rest even though nothing is
			// pushing it.
			if (body->IsStatic || body->IsKinematic)
				continue;

			if (glm::length(body->Velocity) > s_SleepVelocityThreshold)
				return false;
		}

		return true;
	}

	void Physics::SimulateSubstep(float timestep)
	{
		// Apply gravity to dynamic bodies. Static and kinematic bodies ignore it,
		// and sleeping bodies are not simulated at all.
		for (auto* body : s_Bodies)
		{
			if (!body->IsStatic && !body->IsKinematic && !body->IsSleeping)
			{
				body->Velocity.y += s_Gravity.y * timestep;
			}
		}

		DetectCollisions();
		ResolveCollisions(timestep);
		IntegrateBodies(timestep);
		HandleSleeping(timestep);
	}

	uint32_t Physics::ComputeSubstepCount(float timestep)
	{
		// The thinnest collider sets the scale: a body must never travel more
		// than half of it in one substep, or it could pass through.
		float thinnestCollider = 0.0f;
		float largestDisplacement = 0.0f;

		for (PhysicsBody* body : s_Bodies)
		{
			if (body->Shape == PhysicsBody::ShapeType::Sphere)
			{
				thinnestCollider = thinnestCollider > 0.0f
					? std::min(thinnestCollider, body->SphereRadius * 2.0f)
					: body->SphereRadius * 2.0f;
			}
			else
			{
				const float thickness = std::min({ body->BoxSize.x, body->BoxSize.y, body->BoxSize.z });
				thinnestCollider = thinnestCollider > 0.0f
					? std::min(thinnestCollider, thickness)
					: thickness;
			}

			// Static bodies never move, so they cannot tunnel.
			if (body->IsStatic)
				continue;

			// Include the velocity the body will gain from acceleration this step,
			// otherwise a body that is stationary now but accelerating would be
			// under-counted.
			const glm::vec3 stepVelocity = body->Velocity + body->Acceleration * timestep;
			largestDisplacement = std::max(largestDisplacement, glm::length(stepVelocity) * timestep);
		}

		// A degenerate collider would divide by zero, and a stationary scene
		// needs no extra work.
		if (thinnestCollider <= 0.0f || largestDisplacement <= 0.0f)
			return 1;

		const float maxStepDistance = thinnestCollider * 0.5f;
		const uint32_t needed = static_cast<uint32_t>(std::ceil(largestDisplacement / maxStepDistance));
		return std::clamp(needed, 1u, s_MaxSubsteps);
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
			// A kinematic body advances purely from its own velocity: it ignores
			// acceleration and cannot be pushed by a collision.
			if (body->IsKinematic)
			{
				if (!body->IsSleeping)
				{
					body->Position += body->Velocity * timestep;
					body->Velocity = glm::vec3(0.0f);
				}
				continue;
			}

			if (body->IsStatic || body->IsSleeping)
				continue;

			// Update velocity
			body->Velocity += body->Acceleration * timestep;

			// Safety clamp. With CCD enabled this should never be reached, since
			// substepping already keeps each substep shorter than the thinnest
			// collider; it remains as a guard against a pathological timestep or a
			// degenerate scene where CCD cannot subdivide further.
			constexpr float kMaxVelocity = 1000.0f;
			const float speed = glm::length(body->Velocity);
			if (speed > kMaxVelocity)
			{
				body->Velocity = glm::normalize(body->Velocity) * kMaxVelocity;
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
		// Deliberately appends rather than clearing: Update() clears once per
		// frame, and with substepping the same pair can touch on several
		// substeps. Only distinct pairs are recorded.
		for (size_t i = 0; i < s_Bodies.size(); i++)
		{
			for (size_t j = i + 1; j < s_Bodies.size(); j++)
			{
				PhysicsBody* bodyA = s_Bodies[i];
				PhysicsBody* bodyB = s_Bodies[j];

				// Skip if neither body can be pushed by a collision: a static/static
				// pair can never move, so resolving it would be wasted work.
				if (bodyA->IsStatic && bodyB->IsStatic)
					continue;

				// The layers have to agree: each body has to be on a layer the
				// other's mask names. A trigger on a layer the player's mask
				// does not name is not something the player walks into.
				if (!CollisionLayersAgree(bodyA->CollisionLayer, bodyA->CollisionMask,
					bodyB->CollisionLayer, bodyB->CollisionMask))
				{
					continue;
				}

				// Spheres use exact sphere-sphere overlap; everything else falls
				// back to the axis-aligned box test.
				const bool touching = bodyA->Shape == PhysicsBody::ShapeType::Sphere &&
					bodyB->Shape == PhysicsBody::ShapeType::Sphere
					? bodyA->IntersectsSphere(*bodyB)
					: bodyA->IntersectsAABB(*bodyB);

				if (!touching)
					continue;

				// Order the pair so the key is stable regardless of body
				// insertion order into s_Bodies.
				const ContactKey key{ bodyA->ID < bodyB->ID ? bodyA->ID : bodyB->ID,
				                      bodyA->ID < bodyB->ID ? bodyB->ID : bodyA->ID };

				const bool alreadyRecorded = std::find(s_ActiveContacts.begin(), s_ActiveContacts.end(), key)
					!= s_ActiveContacts.end();
				if (!alreadyRecorded)
					s_ActiveContacts.push_back(key);
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

				// The layers have to agree, here as in the detection: a body
				// that does not collide with another one is not pushed by it
				// either.
				if (!CollisionLayersAgree(bodyA->CollisionLayer, bodyA->CollisionMask,
					bodyB->CollisionLayer, bodyB->CollisionMask))
				{
					continue;
				}

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

					// Collision normal, pointing from A towards B along the axis of least
					// overlap. If A's extent starts below B's on this axis, B is the
					// one that must be pushed up, so the normal is +axis.
					glm::vec3 normal = glm::vec3(0.0f);
					if (overlapAxis == 0)
					{
						normal = (minA.x < minB.x) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(-1.0f, 0.0f, 0.0f);
					}
					else if (overlapAxis == 1)
					{
						normal = (minA.y < minB.y) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, -1.0f, 0.0f);
					}
					else
					{
						normal = (minA.z < minB.z) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
					}

					// Resolve position (positional correction), weighted by inverse mass so an
					// immovable partner (static or kinematic) absorbs none of the
					// correction and the movable body takes all of it. A naive
					// 50/50 split would let a kinematic platform sink into the
					// dynamic body that rests on it.
					const float totalInverseMass = bodyA->InverseMass + bodyB->InverseMass;
					if (totalInverseMass > 0.0f)
					{
						const float correction = minOverlap + 0.01f; // Small epsilon to prevent sinking
						const glm::vec3 separation = normal * correction;

						bodyA->Position -= separation * (bodyA->InverseMass / totalInverseMass);
						bodyB->Position += separation * (bodyB->InverseMass / totalInverseMass);
					}

					// Resolve velocity (impulse-based). A kinematic body participates here: its
					// velocity is what carries the platform, so a dynamic body
					// landing on it inherits the platform's motion.
					//
					// `normal` points from A towards B, so B approaching A means
					// (vA - vB) projects positively onto it. A negative projection
					// therefore means the pair is already separating and an
					// impulse would only add energy.
					glm::vec3 relativeVelocity = bodyA->Velocity - bodyB->Velocity;
					const float velocityAlongNormal = glm::dot(relativeVelocity, normal);

					// Separating already: nothing to cancel.
					if (velocityAlongNormal < 0.0f)
						continue;

					// Wake up sleeping bodies on collision
					if (bodyA->IsSleeping)
						bodyA->IsSleeping = false;
					if (bodyB->IsSleeping)
						bodyB->IsSleeping = false;

					// Both partners immovable: nothing to impart.
					if (totalInverseMass <= 0.0f)
						continue;

					float e = std::min(bodyA->Restitution, bodyB->Restitution);
					float j = -(1.0f + e) * velocityAlongNormal;
					j /= totalInverseMass;

					glm::vec3 impulse = j * normal;

					bodyA->Velocity += impulse * bodyA->InverseMass;
					bodyB->Velocity -= impulse * bodyB->InverseMass;

					// Apply friction, clamped by the normal impulse so friction can never
					// reverse the tangential motion (the classic friction bug).
					glm::vec3 tangent = relativeVelocity - normal * velocityAlongNormal;
					const float tangentLength = glm::length(tangent);
					if (tangentLength > 0.0f)
					{
						tangent = glm::normalize(tangent);

						const float frictionCoefficient = std::sqrt(bodyA->Friction * bodyB->Friction);
						float jF = -glm::dot(relativeVelocity, tangent) * frictionCoefficient;
						jF = std::max(jF, -j * frictionCoefficient);
						jF /= totalInverseMass;

						const glm::vec3 frictionImpulse = jF * tangent;

						bodyA->Velocity += frictionImpulse * bodyA->InverseMass;
						bodyB->Velocity -= frictionImpulse * bodyB->InverseMass;
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

	bool Physics::RaycastDistance(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
		glm::vec3& hitPosition, glm::vec3& hitNormal, float& hitDistance, uint64_t& hitBodyID)
	{
		if (!Raycast(origin, direction, maxDistance, hitPosition, hitNormal, hitBodyID))
			return false;

		// The distance is the length of the line from the origin to the hit.
		hitDistance = glm::length(hitPosition - origin);
		return true;
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
			// Static and kinematic bodies never sleep. A kinematic body is driven
			// by its velocity, so parking it would silently stop it moving.
			if (body->IsStatic || body->IsKinematic)
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
