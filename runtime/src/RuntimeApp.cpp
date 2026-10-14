#include "fgpch.h"
#include "runtime/src/RuntimeApp.h"

#include "Forge/Scene/Components.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Scene/SceneSerializer.h"

namespace Forge {

	RuntimeApp::RuntimeApp()
		: Application(ApplicationSpecification{ "Forge Runtime", "" })
	{
		// Initialize renderer
		Renderer::Init();

		// Load scene
		m_Scene = CreateRef<Scene>();

		// Create a simple test scene
		auto ground = m_Scene->CreateEntity("Ground");
		auto& groundTransform = ground.AddComponent<TransformComponent>();
		groundTransform.Translation = { 0.0f, -1.0f, 0.0f };
		groundTransform.Scale = { 10.0f, 1.0f, 10.0f };

		auto camera = m_Scene->CreateEntity("Camera");
		auto& cameraTransform = camera.AddComponent<TransformComponent>();
		cameraTransform.Translation = { 0.0f, 5.0f, 10.0f };
		auto& cameraComp = camera.AddComponent<CameraComponent>();
		cameraComp.Primary = true;

		auto light = m_Scene->CreateEntity("Light");
		auto& lightComp = light.AddComponent<LightComponent>();
		lightComp.Type = LightComponent::Type::Directional;
	}

	RuntimeApp::~RuntimeApp()
	{
	}

	void RuntimeApp::Run()
	{
		while (m_Running)
		{
			float time = (float)glfwGetTime();
			Timestep timestep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			if (!m_Minimized)
			{
				for (Layer* layer : m_LayerStack)
					layer->OnUpdate(timestep);

				if (m_Scene)
				{
					m_Scene->OnUpdateRuntime(timestep);
				}
			}

			m_Window->OnUpdate();
			ExecuteMainThreadQueue();
		}
	}

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new RuntimeApp();
	}

}
