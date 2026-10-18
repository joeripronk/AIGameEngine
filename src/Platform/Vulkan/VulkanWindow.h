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

		bool IsValid() const override { return m_Window != nullptr; }

		glm::vec2 GetScrollOffset()
		{
			glm::vec2 offset = { (float)m_Data.ScrollOffsetX, (float)m_Data.ScrollOffsetY };
			m_Data.ScrollOffsetX = 0.0;
			m_Data.ScrollOffsetY = 0.0;
			return offset;
		}

	private:
		struct WindowData
		{
			std::string Title;
			uint32_t Width;
			uint32_t Height;
			bool VSync = true;
			EventCallbackFn EventCallback;
			double ScrollOffsetX = 0.0;
			double ScrollOffsetY = 0.0;
		};

		GLFWwindow* m_Window = nullptr;
		WindowData m_Data;
	};

}
