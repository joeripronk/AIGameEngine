#include "fgpch.h"
#include "Platform/Vulkan/VulkanWindow.h"

namespace Forge {

	static uint32_t s_GLFWWindowCount = 0;

	VulkanWindow::VulkanWindow(const WindowProps& props)
	{
		Init(props);
	}

	VulkanWindow::~VulkanWindow()
	{
		Shutdown();
	}

	void VulkanWindow::Init(const WindowProps& props)
	{
		m_Data.Title = props.Title;
		m_Data.Width = props.Width;
		m_Data.Height = props.Height;

		FG_CORE_INFO("Creating window {0} ({1}, {2})", m_Data.Title, m_Data.Width, m_Data.Height);

		if (s_GLFWWindowCount == 0)
		{
			int result = glfwInit();
			FG_CORE_ASSERT(result, "Could not initialize GLFW!");

			glfwSetErrorCallback([](int error, const char* description)
			{
				FG_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
			});
		}

		s_GLFWWindowCount++;

		m_Window = glfwCreateWindow((int)m_Data.Width, (int)m_Data.Height, m_Data.Title.c_str(), nullptr, nullptr);
		FG_CORE_ASSERT(m_Window, "Could not create GLFW window!");

		glfwSetWindowUserPointer(m_Window, &m_Data);
		SetVSync(true);
	}

	void VulkanWindow::Shutdown()
	{
		glfwDestroyWindow(m_Window);
		s_GLFWWindowCount--;

		if (s_GLFWWindowCount == 0)
			glfwTerminate();
	}

	void VulkanWindow::OnUpdate()
	{
		glfwPollEvents();
	}

	void VulkanWindow::SetVSync(bool enabled)
	{
		glfwSwapInterval(enabled ? 1 : 0);
		m_Data.VSync = enabled;
	}

}
