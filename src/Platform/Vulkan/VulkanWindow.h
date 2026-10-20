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

		uint32_t GetFramebufferWidth() const override { return m_Data.FramebufferWidth; }
		uint32_t GetFramebufferHeight() const override { return m_Data.FramebufferHeight; }

		void SetSize(uint32_t width, uint32_t height) override;

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
			// The window's size in pixels. It is not the width and height
			// above: on a scaled display the window is measured in points and
			// the framebuffer - the thing that is actually presented - is
			// measured in pixels.
			uint32_t FramebufferWidth;
			uint32_t FramebufferHeight;
			bool VSync = true;
			EventCallbackFn EventCallback;
			double ScrollOffsetX = 0.0;
			double ScrollOffsetY = 0.0;
		};

		GLFWwindow* m_Window = nullptr;
		WindowData m_Data;
	};

}
