#pragma once

#include "Forge/Core/Application.h"
#include "Forge/Scene/Scene.h"

namespace Forge {

	class RuntimeApp : public Application
	{
	public:
		RuntimeApp(const ApplicationCommandLineArgs& args);
		~RuntimeApp() override = default;

		void Run() override;

	private:
		// Loads a scene from disk (defaults to the comprehensive test scene).
		bool LoadScene(const std::string& path);
		// Falls back to a small built-in scene if loading fails.
		void CreateFallbackScene();

		Ref<Scene> m_Scene;
	};

}