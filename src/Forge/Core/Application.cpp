#include "fgpch.h"
#include "Forge/Core/Application.h"
#include "Forge/Core/Layer.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Debug/Instrumentor.h"
#include "Forge/Core/InputMap.h"
#include "Forge/Core/Input.h"
#include "Forge/Events/KeyEvent.h"
#include "Forge/Events/MouseEvent.h"
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
		if (!m_Window || !m_Window->IsValid())
		{
			// Startup failed for a reason the platform logged (no display,
			// no way to open a window). Continuing here would run the renderer
			// on a null surface handle. Run() returns, the entry point exits 1.
			FG_CORE_ERROR("Application startup aborted: no window was created");
			m_Running = false;
			return;
		}

		m_Window->SetEventCallback(FG_BIND_EVENT_FN(Application::OnEvent));

		// Hand the native window to the renderer so it can create the Vulkan
		// surface and presentation swapchain, along with its size so every
		// render target is created at the size the swapchain will really be.
		//
		// The size is the framebuffer's - in pixels. The renderer draws into
		// the surface, and the surface is the framebuffer; the window's size
		// in points is what the user interface lays itself out in.
		Renderer::SetActiveWindowHandle(m_Window->GetNativeWindow());
		Renderer::SetActiveWindowSize(m_Window->GetFramebufferWidth(), m_Window->GetFramebufferHeight());

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

		// The constructor reaches this point only with a valid window; the
		// answer is latched here because the entry point asks after Run()
		// returns, when a normally-closed window has long been destroyed.
		m_StartupSucceeded = true;
		FG_CORE_INFO("Application: startup latched OK");
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
		// The input action map is fed by the events: a press is an event,
		// and the map's answers ("is 'jump' held?", "was 'jump' pressed
		// this frame?") are exact because of it.
		InputMap::OnEvent(e);

		// The platform's own state is fed by them too. A desktop window
		// keeps that state and is asked directly, so this changes nothing
		// there; on a platform with no window to ask - the phone - the
		// events are the state.
		if (KeyPressedEvent* pressed = e.TryCast<KeyPressedEvent>())
			Input::SetKeyPressed(static_cast<KeyCode>(pressed->GetKeyCode()), true);
		else if (KeyReleasedEvent* released = e.TryCast<KeyReleasedEvent>())
			Input::SetKeyPressed(static_cast<KeyCode>(released->GetKeyCode()), false);
		else if (MouseButtonPressedEvent* button = e.TryCast<MouseButtonPressedEvent>())
			Input::SetMouseButtonPressed(static_cast<MouseCode>(button->GetMouseButton()), true);
		else if (MouseButtonReleasedEvent* button = e.TryCast<MouseButtonReleasedEvent>())
			Input::SetMouseButtonPressed(static_cast<MouseCode>(button->GetMouseButton()), false);
		else if (MouseMovedEvent* moved = e.TryCast<MouseMovedEvent>())
			Input::SetMousePosition(moved->GetX(), moved->GetY());

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

	void Application::PollEvents()
	{
		if (!m_Window)
			return;

		// The transient state is cleared before the events are read: the
		// events that arrive now are this frame's, and the ones from the
		// frame that ended are not.
		InputMap::BeginFrame();

		m_Window->OnUpdate();
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

			PollEvents();

			ExecuteMainThreadQueue();
		}
	}

	bool Application::OnWindowClose(WindowCloseEvent& e)
	{
		(void)e;
		FG_CORE_INFO("Application: window close -> stopping");
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
		// The renderer works in pixels - the framebuffer is what is
		// presented, and on a scaled display it is not the window's size in
		// points. Handing it the point size is what left the overlay
		// oversized and the scene at the old size: the overlay lays itself
		// out in the window's size and the scene is drawn at the
		// framebuffer's, and the two only agree on an unscaled display.
		Renderer::OnWindowResize(e.GetFramebufferWidth(), e.GetFramebufferHeight());
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
