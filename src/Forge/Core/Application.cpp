#include "fgpch.h"
#include "Forge/Core/Application.h"
#include "Forge/Core/Layer.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Debug/Instrumentor.h"
#include "Forge/Core/Time.h"

#include <algorithm>

namespace Forge {

	Application* Application::s_Instance = nullptr;

	Application::Application(const ApplicationSpecification& specification)
		: m_Specification(specification)
	{
		FG_CORE_ASSERT_MSG(!s_Instance, "Application already exists!");
		s_Instance = this;

		m_Window = Window::Create(WindowProps{ specification.Name, 1600, 900 });
		m_Window->SetEventCallback(FG_BIND_EVENT_FN(Application::OnEvent));

		// Hand the native window to the renderer so it can create the Vulkan
		// surface and presentation swapchain, along with its size so every
		// render target is created at the size the swapchain will really be.
		Renderer::SetActiveWindowHandle(m_Window->GetNativeWindow());
		Renderer::SetActiveWindowSize(m_Window->GetWidth(), m_Window->GetHeight());

		// Profiling is opt-in (the Hazel rule: a session is explicit, not
		// recorded silently on every launch): --profile writes profile.json,
		// --profile=path or a following argument names the trace file.
		if (const std::string path = ProfilePathFromArgs(specification.CommandLineArgs);
			!path.empty())
		{
			if (ProfileBeginSession(specification.Name + " Profile Session", path))
				FG_CORE_INFO("Profiling to '{0}'", path);
			else
				FG_CORE_ERROR("Could not open profiling output file '{0}'", path);
		}
	}

	std::string Application::GetArgumentValue(const ApplicationCommandLineArgs& args, const std::string& option)
	{
		for (int i = 0; i < args.Count; ++i)
		{
			const std::string arg = args[i];

			if (arg == option)
			{
				// The next argument names the value only if it is not another
				// option: `--profile --other` must not swallow `--other`.
				if (i + 1 < args.Count && args[i + 1][0] != '-')
					return args[i + 1];
				return {};
			}

			if (arg.rfind(option + "=", 0) == 0)
				return arg.substr(option.size() + 1);
		}
		return {};
	}

	std::string Application::ProfilePathFromArgs(const ApplicationCommandLineArgs& args)
	{
		// An explicit path wins; a bare --profile gets the conventional name.
		if (const std::string path = GetArgumentValue(args, "--profile"); !path.empty())
			return path;

		for (int i = 0; i < args.Count; ++i)
		{
			if (std::string(args[i]) == "--profile")
				return "profile.json";
		}
		return {};
	}

	Application::~Application()
	{
		ProfileEndSession();
		Renderer::Shutdown();
	}

	void Application::OnEvent(Event& e)
	{
		EventDispatcher dispatcher(e);
		dispatcher.Dispatch<WindowCloseEvent>(FG_BIND_EVENT_FN(Application::OnWindowClose));
		dispatcher.Dispatch<WindowResizeEvent>(FG_BIND_EVENT_FN(Application::OnWindowResize));

		for (auto it = m_LayerStack.end(); it != m_LayerStack.begin(); )
		{
			(*--it)->OnEvent(e);
			if (e.Handled)
				break;
		}
	}

	void Application::PushLayer(Layer* layer)
	{
		m_LayerStack.PushLayer(layer);
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* overlay)
	{
		m_LayerStack.PushOverlay(overlay);
		overlay->OnAttach();
	}

	void Application::Close()
	{
		m_Running = false;
	}

	void Application::Run()
	{
		while (m_Running)
		{
			FG_PROFILE_SCOPE("Frame");

			float time = (float)Time::GetTime();
			Timestep timestep = time - m_LastFrameTime;
			m_LastFrameTime = time;

			if (!m_Minimized)
			{
				for (Layer* layer : m_LayerStack)
					layer->OnUpdate(timestep);
			}

			m_Window->OnUpdate();

			ExecuteMainThreadQueue();
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		(void)e;
		Close();
		return true;
	}

	bool Application::OnWindowResize(WindowResizeEvent& e)
	{
		if (e.GetWidth() == 0 || e.GetHeight() == 0)
		{
			m_Minimized = true;
			return false;
		}

		m_Minimized = false;

		// Keep the renderer's presentation target in sync with the window.
		Renderer::OnWindowResize(e.GetWidth(), e.GetHeight());
		return false;
	}

	void Application::ExecuteMainThreadQueue()
	{
		std::lock_guard<std::mutex> lock(m_MainThreadQueueMutex);
		for (auto& function : m_MainThreadQueue)
			function();
		m_MainThreadQueue.clear();
	}

	void Application::SubmitToMainThread(const std::function<void()>& function)
	{
		std::lock_guard<std::mutex> lock(m_MainThreadQueueMutex);
		m_MainThreadQueue.push_back(function);
	}

}
