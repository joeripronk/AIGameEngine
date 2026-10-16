#include "fgpch.h"
#include "Forge/Core/Application.h"
#include "Forge/Core/Layer.h"
#include "Forge/Renderer/Renderer.h"

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
		// surface and presentation swapchain.
		Renderer::SetActiveWindowHandle(m_Window->GetNativeWindow());
	}

	Application::~Application()
	{
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
			float time = (float)glfwGetTime();
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
