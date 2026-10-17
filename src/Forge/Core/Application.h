#pragma once

#include "Forge/Core/Base.h"

#include "Forge/Core/Window.h"
#include "Forge/Core/LayerStack.h"
#include "Forge/Events/Event.h"
#include "Forge/Events/ApplicationEvent.h"

#include "Forge/Core/Timestep.h"

namespace Forge {

	struct ApplicationCommandLineArgs
	{
		int Count = 0;
		char** Args = nullptr;

		const char* operator[](int index) const
		{
			FG_CORE_ASSERT_MSG(index < Count, "Assertion failed!");
			return Args[index];
		}
	};

	struct ApplicationSpecification
	{
		std::string Name = "Forge Application";
		std::string WorkingDirectory;
		ApplicationCommandLineArgs CommandLineArgs;
	};

	class Application
	{
	public:
		Application(const ApplicationSpecification& specification);
		virtual ~Application();

		virtual void Run();

		void OnEvent(Event& e);

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);

		Window& GetWindow() { return *m_Window; }

		void Close();

		static Application& Get() { return *s_Instance; }

		// False when no Application exists (headless tools, unit tests).
		// Get() dereferences a null or already-destroyed instance in that case,
		// so anything optional must check this first.
		static bool HasInstance() { return s_Instance != nullptr; }

		const ApplicationSpecification& GetSpecification() const { return m_Specification; }

		void SubmitToMainThread(const std::function<void()>& function);

		// Accessors for runtime/editor
		bool IsRunning() const { return m_Running; }
		float GetLastFrameTime() const { return m_LastFrameTime; }
		bool IsMinimized() const { return m_Minimized; }
		LayerStack& GetLayerStack() { return m_LayerStack; }
		Scope<Window>& GetWindowScope() { return m_Window; }
		std::vector<std::function<void()>>& GetMainThreadQueue() { return m_MainThreadQueue; }
		std::mutex& GetMainThreadQueueMutex() { return m_MainThreadQueueMutex; }

	protected:
		bool OnWindowClose(WindowCloseEvent& e);
		bool OnWindowResize(WindowResizeEvent& e);

		void ExecuteMainThreadQueue();
	private:
		ApplicationSpecification m_Specification;
		Scope<Window> m_Window;
		bool m_Running = true;
		bool m_Minimized = false;
		LayerStack m_LayerStack;
		float m_LastFrameTime = 0.0f;

		std::vector<std::function<void()>> m_MainThreadQueue;
		std::mutex m_MainThreadQueueMutex;
	private:
		static Application* s_Instance;
		friend int main(int argc, char** argv);
	};

	// To be defined in CLIENT
	Application* CreateApplication(ApplicationCommandLineArgs args);

}
