#include "fgpch.h"
#include "runtime/src/RuntimeApp.h"

#include "Forge/Core/Log.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Scripting/ScriptEngine.h"

#include <string>

namespace Forge {

	static const char* kDefaultScene = "resources/scenes/EngineTest.forge";

	RuntimeApp::RuntimeApp(const ApplicationCommandLineArgs& args)
		: Application(ApplicationSpecification{ "Forge Runtime", "", args })
	{
		Renderer::Init();

		// Determine the scene to load: the first user argument (argv[1];
		// argv[0] is the program name) takes precedence.
		std::string scenePath = kDefaultScene;
		if (GetSpecification().CommandLineArgs.Count > 1)
		{
			std::string candidate = GetSpecification().CommandLineArgs[1];
			if (!candidate.empty())
				scenePath = candidate;
		}

		if (!LoadScene(scenePath))
			CreateFallbackScene();
	}

	RuntimeApp::~RuntimeApp()
	{
	}

	bool RuntimeApp::LoadScene(const std::string& path)
	{
		auto scene = CreateRef<Scene>();
		SceneSerializer serializer(scene);
		if (!serializer.Deserialize(path))
		{
			FG_CORE_ERROR("Runtime: failed to load scene '{0}'", path);
			return false;
		}

		m_Scene = scene;
		FG_CORE_INFO("Runtime: loaded scene '{0}'", path);
		return true;
	}

	void RuntimeApp::CreateFallbackScene()
	{
		FG_CORE_WARN("Runtime: using built-in fallback scene");

		m_Scene = CreateRef<Scene>();

		auto camera = m_Scene->CreateEntity("MainCamera");
		camera.GetComponent<TransformComponent>().Translation = { 0.0f, 5.0f, 12.0f };
		auto& cameraComponent = camera.AddComponent<CameraComponent>();
		cameraComponent.Primary = true;
		cameraComponent.FieldOfView = 45.0f;
		cameraComponent.AspectRatio = 16.0f / 9.0f;
		cameraComponent.UpdateProjection();

		auto light = m_Scene->CreateEntity("DirectionalLight");
		auto& lightComponent = light.AddComponent<LightComponent>();
		lightComponent.Type = LightComponent::Type::Directional;
		lightComponent.Direction = { -0.5f, -1.0f, -0.3f };

		auto cube = m_Scene->CreateEntity("Cube");
		cube.GetComponent<TransformComponent>().Translation = { 0.0f, 0.5f, 0.0f };
		cube.AddComponent<PBRMaterialComponent>().Albedo = { 0.8f, 0.3f, 0.3f };

		auto physics = m_Scene->CreateEntity("PhysicsBox");
		physics.GetComponent<TransformComponent>().Translation = { 0.0f, 5.0f, 0.0f };
		physics.AddComponent<RigidbodyComponent>().Type = RigidbodyComponent::BodyType::Dynamic;
		physics.AddComponent<BoxColliderComponent>().Size = { 1.0f, 1.0f, 1.0f };

		// A scripted entity so the runtime exercises the Lua pipeline.
		auto scripted = m_Scene->CreateEntity("ScriptedMover");
		scripted.GetComponent<TransformComponent>().Translation = { 3.0f, 0.5f, 0.0f };
		scripted.AddComponent<ScriptComponent>().ClassName = "Mover";
	}

	void RuntimeApp::Run()
	{
		float lastFrameTime = (float)glfwGetTime();
		float frameAccumulator = 0.0f;
		const float fixedStep = 1.0f / 60.0f;
		uint64_t frameCount = 0;

		while (IsRunning())
		{
			float time = (float)glfwGetTime();
			float frameTime = time - lastFrameTime;
			lastFrameTime = time;

			if (!IsMinimized())
			{
				for (Layer* layer : GetLayerStack())
					layer->OnUpdate(Timestep(frameTime));

				if (m_Scene)
				{
					// Fixed-step the simulation so physics/scripting are stable
					// regardless of frame rate.
					frameAccumulator += frameTime;
					while (frameAccumulator >= fixedStep)
					{
						if (!m_Scene->IsRunning())
							m_Scene->OnRuntimeStart();

						m_Scene->OnUpdateRuntime(Timestep(fixedStep));
						frameAccumulator -= fixedStep;
						frameCount++;
					}
				}
			}

			GetWindowScope()->OnUpdate();
			ExecuteMainThreadQueue();

			// Auto-exit after a few seconds when running headless (no window
			// close event is delivered) so CI/verification doesn't hang.
			if (frameCount >= 180)
			{
				FG_CORE_INFO("Runtime: ran {0} simulation frames", frameCount);
				Close();
			}
		}

		if (m_Scene && m_Scene->IsRunning())
			m_Scene->OnRuntimeStop();
	}

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new RuntimeApp(args);
	}

}