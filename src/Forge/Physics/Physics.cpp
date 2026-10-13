#include "fgpch.h"
#include "Forge/Physics/Physics.h"

namespace Forge {

	void* Physics::s_PhysicsWorld = nullptr;

	void Physics::Init()
	{
		// Initialize physics world (e.g., Box2D or PhysX)
		// TODO: Implement physics initialization
	}

	void Physics::Shutdown()
	{
		// Shutdown physics world
		// TODO: Implement physics shutdown
	}

	void Physics::OnSceneStart()
	{
		// TODO: Initialize physics bodies from scene components
	}

	void Physics::OnSceneStop()
	{
		// TODO: Cleanup physics bodies
	}

	void Physics::Update(float timestep)
	{
		// TODO: Step physics simulation
	}

}
