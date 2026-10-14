#include "fgpch.h"
#include "Platform/Vulkan/VulkanWindow.h"

#include "Forge/Events/ApplicationEvent.h"

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

		// Set up GLFW callbacks
		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.EventCallback(WindowCloseEvent());
		});

		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.Width = width;
			data.Height = height;
			data.EventCallback(WindowResizeEvent((uint32_t)width, (uint32_t)height));
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			switch (action)
			{
				case GLFW_PRESS:
					data.EventCallback(KeyPressedEvent(key, 0));
					break;
				case GLFW_RELEASE:
					data.EventCallback(KeyReleasedEvent(key));
					break;
				case GLFW_REPEAT:
					data.EventCallback(KeyPressedEvent(key, 1));
					break;
			}
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			switch (action)
			{
				case GLFW_PRESS:
					data.EventCallback(MouseButtonPressedEvent(button));
					break;
				case GLFW_RELEASE:
					data.EventCallback(MouseButtonReleasedEvent(button));
					break;
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xoffset, double yoffset)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.EventCallback(MouseScrolledEvent((float)xoffset, (float)yoffset));
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.EventCallback(MouseMovedEvent((float)x, (float)y));
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int keycode)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.EventCallback(KeyTypedEvent(keycode));
		});

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
