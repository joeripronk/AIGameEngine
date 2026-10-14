#pragma once

#include "Forge/Core/Application.h"
#include "Forge/Scene/Scene.h"

namespace Forge {

	class RuntimeApp : public Application
	{
	public:
		RuntimeApp();
		~RuntimeApp() override;

		void Run() override;

	private:
		Ref<Scene> m_Scene;
	};

}
