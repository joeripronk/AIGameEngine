#include "fgpch.h"
#include "Forge/Debug/Instrumentor.h"
#include "runtime/src/RuntimeApp.h"

#include "Forge/Core/Log.h"
#include "Forge/Core/Time.h"
#include "Forge/Project/Project.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Scripting/ScriptEngine.h"

#include <GLFW/glfw3.h>
#include <string>
#include <cstdlib>
#include <algorithm>

namespace Forge {

	static const char* kDefaultScene = "resources/scenes/EngineTest.forge";

	RuntimeApp::RuntimeApp(const ApplicationCommandLineArgs& args)
		: Application(ApplicationSpecification{ "Forge Runtime", "", args })
	{
		// No window, no surface, no renderer - the base has logged why.
		if (!StartupSucceeded())
			return;

		Renderer::Init();

		// A project file names the scene the game starts in. It is loaded first
		// so the start scene is available; a failure is logged by Project and
		// leaves the fallback below in charge.
		if (const std::string project = Application::GetArgumentValue(args, "--project"); !project.empty())
		{
			if (Project::Load(project))
				FG_CORE_INFO("Runtime: loaded project '{0}'", project);
			else
				FG_CORE_ERROR("Runtime: could not load project '{0}'", project);
		}

		// Determine the scene to load. Precedence: an explicit scene argument
		// (argv[1]; argv[0] is the program name), then the project's start
		// scene, then the engine's own test scene.
		std::string scenePath;
		if (GetSpecification().CommandLineArgs.Count > 1)
		{
			std::string candidate = GetSpecification().CommandLineArgs[1];
			if (!candidate.empty() && candidate[0] != '-')
				scenePath = candidate;
		}

		if (scenePath.empty())
			scenePath = Project::GetStartScenePath().string();

		if (scenePath.empty())
			scenePath = kDefaultScene;

		if (!LoadScene(scenePath))
			CreateFallbackScene();
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
		float lastFrameTime = (float)Time::GetTime();
		float frameAccumulator = 0.0f;
		const float fixedStep = 1.0f / 60.0f;
		uint64_t frameCount = 0;

		while (IsRunning())
		{
			FG_PROFILE_SCOPE("Frame");
			float time = (float)Time::GetTime();
			float frameTime = time - lastFrameTime;
			lastFrameTime = time;

			if (!IsMinimized())
			{
				for (Layer* layer : GetLayerStack())
					layer->OnUpdate(Timestep(frameTime));

				// Acquire a swapchain image, open the frame's GPU command list,
				// drive the scene, then submit and present.
				const uint32_t swapchainImage = Renderer::AcquireSwapchainImage();

				Renderer::BeginScene();

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

					// Draw the scene's meshes into the offscreen HDR target, then run
					// the post chain (SSAO -> tonemap -> final composite) to get it
					// onto the acquired swapchain image. RenderScene alone only fills
					// the HDR target, so without these the acquired image is never
					// drawn to and the window stays black.
					Renderer::RenderScene(m_Scene.get(), swapchainImage);
					Renderer::BeginSSAOPass();
					Renderer::BeginBloomPass();
					Renderer::BeginHDRPass();
					Renderer::BeginFinalPass();
				}

				// Close and submit the frame's GPU work.
				Renderer::EndScene();

				// Optionally read the rendered frame back to disk for
				// verification (FG_CAPTURE_PATH=/tmp/frame.ppm).
				if (const char* capturePath = std::getenv("FG_CAPTURE_PATH"))
				{
					// A capture that silently failed would leave a verification
					// script staring at a stale or missing image, so report it.
					if (!Renderer::CaptureSwapchainImage(swapchainImage, capturePath))
					{
						FG_CORE_ERROR("Runtime: failed to capture frame to '{0}'", capturePath);
					}
				}

				Renderer::PresentSwapchainImage(swapchainImage);
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
