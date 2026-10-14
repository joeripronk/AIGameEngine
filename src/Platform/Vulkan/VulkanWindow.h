#pragma once

#include "Forge/Core/Window.h"

namespace Forge {

	class VulkanWindow : public Window
	{
	public:
		VulkanWindow(const WindowProps& props);
		~VulkanWindow() override;

		void Init(const WindowProps& props);
		void Shutdown();

		void OnUpdate() override;

		uint32_t GetWidth() const override { return m_Data.Width; }
		uint32_t GetHeight() const override { return m_Data.Height; }

		void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
		void SetVSync(bool enabled) override;
		bool IsVSync() const override { return m_Data.VSync; }

		void* GetNativeWindow() const override { return m_Window; }

	private:
		struct WindowData
		{
			std::string Title;
			uint32_t Width;
			uint32_t Height;
			bool VSync = true;
			EventCallbackFn EventCallback;
		};

		GLFWwindow* m_Window = nullptr;
		WindowData m_Data;
	};

}
