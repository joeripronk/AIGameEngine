#include "fgpch.h"
#include "Platform/Vulkan/VulkanWindow.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Forge/Events/ApplicationEvent.h"
#include "Forge/Events/KeyEvent.h"
#include "Forge/Events/MouseEvent.h"

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
			// The result was asserted as `1` - literally always true - so a
			// headless session failed GLFW init, proceeded, and the failure
			// only surfaced at the create-window assert two lines later.
			if (!glfwInit())
			{
				FG_CORE_ERROR("glfwInit failed - no display session to open a window in");
				return;
			}

			glfwSetErrorCallback([](int error, const char* description)
			{
				FG_CORE_ERROR("GLFW Error ({0}): {1}", error, description);
			});
		}

		// Vulkan renders into the window's surface, so GLFW must not create a
		// client-API rendering context of its own.
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		m_Window = glfwCreateWindow((int)m_Data.Width, (int)m_Data.Height, m_Data.Title.c_str(), nullptr, nullptr);
		if (!m_Window)
		{
			// Not an assert: "no display" is a legitimate machine state, and
			// Release builds compile asserts away and kept going with a null
			// handle. The app checks IsValid() and reports startup failure.
			FG_CORE_ERROR("Could not create GLFW window - startup cannot continue");
			return;
		}

		s_GLFWWindowCount++;

		glfwSetWindowUserPointer(m_Window, &m_Data);

		// Set up GLFW callbacks
		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			WindowCloseEvent event;
			data.EventCallback(event);
		});

		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.Width = width;
			data.Height = height;
			WindowResizeEvent event((uint32_t)width, (uint32_t)height);
			data.EventCallback(event);
		});

		glfwSetDropCallback(m_Window, [](GLFWwindow* window, int count, const char** paths)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

			// One event per file. Dropping three scenes onto the window is
			// three opens, and the third one's path is not the drop's
			// meaning.
			for (int i = 0; i < count; ++i)
			{
				WindowDropEvent event(paths[i]);
				data.EventCallback(event);
			}
		});

		glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
		{
			(void)scancode;
			(void)mods;
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			switch (action)
			{
				case GLFW_PRESS:
				{
					KeyPressedEvent event(key, 0);
					data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					KeyReleasedEvent event(key);
					data.EventCallback(event);
					break;
				}
				case GLFW_REPEAT:
				{
					KeyPressedEvent event(key, 1);
					data.EventCallback(event);
					break;
				}
			}
		});

		glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
		{
			(void)mods;
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			switch (action)
			{
				case GLFW_PRESS:
				{
					MouseButtonPressedEvent event(button);
					data.EventCallback(event);
					break;
				}
				case GLFW_RELEASE:
				{
					MouseButtonReleasedEvent event(button);
					data.EventCallback(event);
					break;
				}
			}
		});

		glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xoffset, double yoffset)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.ScrollOffsetX += xoffset;
			data.ScrollOffsetY += yoffset;
			MouseScrolledEvent event((float)xoffset, (float)yoffset);
			data.EventCallback(event);
		});

		glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double x, double y)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			MouseMovedEvent event((float)x, (float)y);
			data.EventCallback(event);
		});

		glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int keycode)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			KeyTypedEvent event(keycode);
			data.EventCallback(event);
		});

		SetVSync(true);
	}

	void VulkanWindow::Shutdown()
	{
		if (!m_Window)
			return;

		glfwDestroyWindow(m_Window);
		m_Window = nullptr;
		s_GLFWWindowCount--;

		if (s_GLFWWindowCount == 0)
			glfwTerminate();
	}

	void VulkanWindow::OnUpdate()
	{
		if (!m_Window)
			return;

		glfwPollEvents();
	}

	void VulkanWindow::SetVSync(bool enabled)
	{
		// NOTE: glfwSwapInterval only applies to OpenGL/GLES contexts. For a
		// Vulkan surface, vsync is configured through the presentation mode
		// chosen at swapchain creation, so we only record the preference here.
		m_Data.VSync = enabled;
	}

}

#endif // FG_NO_VULKAN_RUNTIME
