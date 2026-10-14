#include "fgpch.h"
#include "runtime/src/RuntimeApp.h"

#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Scene/SceneSerializer.h"

namespace Forge {

	RuntimeApp::RuntimeApp()
		: Application(ApplicationSpecification{ "Forge Runtime", "", {0, nullptr} })
	{
		// Initialize renderer
		Renderer::Init();

		// Load scene
		m_Scene = CreateRef<Scene>();

		// Create a simple test scene
		auto ground = m_Scene->CreateEntity("Ground");
		auto& groundTransform = ground.GetComponent<TransformComponent>();
		groundTransform.Translation = { 0.0f, -1.0f, 0.0f };
		groundTransform.Scale = { 10.0f, 1.0f, 10.0f };

		auto camera = m_Scene->CreateEntity("Camera");
		auto& cameraTransform = camera.GetComponent<TransformComponent>();
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
		float lastFrameTime = 0.0f;
		while (IsRunning())
		{
			float time = (float)glfwGetTime();
			Timestep timestep = time - lastFrameTime;
			lastFrameTime = time;

			if (!IsMinimized())
			{
				for (Layer* layer : GetLayerStack())
					layer->OnUpdate(timestep);

				if (m_Scene)
				{
					m_Scene->OnUpdateRuntime(timestep);
				}
			}

			GetWindowScope()->OnUpdate();
			ExecuteMainThreadQueue();
		}
	}

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		(void)args;
		return new RuntimeApp();
	}

}
