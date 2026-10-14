#include "fgpch.h"
#include "Forge/Physics/Physics.h"

#include "Forge/Scene/Components.h"
#include "Forge/Renderer/Mesh.h"

// Box2D
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Forge {

	void* Physics::s_PhysicsWorld = nullptr;

	void Physics::Init()
	{
		// Initialize Box2D physics world
		// Box2D uses 2D, so we'll use it for simple physics
		// For full 3D, we'd use PhysX or Bullet
		s_PhysicsWorld = nullptr; // Not yet implemented
		FG_CORE_INFO("Physics initialized (stub)");
	}

	void Physics::Shutdown()
	{
		if (s_PhysicsWorld)
		{
			// Delete physics world
			// delete s_PhysicsWorld;
			s_PhysicsWorld = nullptr;
		}
		FG_CORE_INFO("Physics shut down");
	}

	void Physics::OnSceneStart()
	{
		// Initialize physics bodies from scene components
		// This would create Box2D bodies for entities with RigidbodyComponent
		FG_CORE_TRACE("Physics scene start");
	}

	void Physics::OnSceneStop()
	{
		// Cleanup physics bodies
		FG_CORE_TRACE("Physics scene stop");
	}

	void Physics::Update(float timestep)
	{
		// Step physics simulation
		// if (s_PhysicsWorld)
		// {
		// 	s_PhysicsWorld->Step(timestep, 6, 2);
		//
		// 	// Update transform components from physics bodies
		// 	// This requires iterating over entities with RigidbodyComponent
		// }
	}

}
