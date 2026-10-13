#pragma once

#include "Forge/Core/Window.h"

#include <GLFW/glfw3.h>

namespace Forge {

	class VulkanWindow : public Window
	{
	public:
		VulkanWindow(const WindowProps& props);
		~VulkanWindow();

		void OnUpdate() override;

		uint32_t GetWidth() const override { return m_Data.Width; }
		uint32_t GetHeight() const override { return m_Data.Height; }

		void SetEventCallback(const EventCallbackFn& callback) override { m_Data.Callback = callback; }
		void SetVSync(bool enabled) override;
		bool IsVSync() const override { return m_Data.VSync; }

		void* GetNativeWindow() const override { return m_Window; }

		GLFWwindow* GetGLFWWindow() const { return m_Window; }
	private:
		void Init(const WindowProps& props);
		void Shutdown();

		GLFWwindow* m_Window;

		struct WindowData
		{
			std::string Title;
			uint32_t Width;
			uint32_t Height;
			bool VSync;

			EventCallbackFn Callback;
		};

		WindowData m_Data;
	};

}
