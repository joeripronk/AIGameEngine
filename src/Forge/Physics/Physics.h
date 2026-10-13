#pragma once

namespace Forge {

	class Physics
	{
	public:
		static void Init();
		static void Shutdown();

		static void OnSceneStart();
		static void OnSceneStop();

		static void Update(float timestep);
	private:
		static void* s_PhysicsWorld;
	};

}
