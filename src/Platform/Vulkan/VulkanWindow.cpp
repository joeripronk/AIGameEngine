#include "fgpch.h"
#include "Platform/Vulkan/VulkanWindow.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Forge/Core/Input.h"
#include "Forge/Events/ApplicationEvent.h"
#include "Forge/Events/JoypadEvent.h"
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
		m_Data.FramebufferWidth = props.Width;
		m_Data.FramebufferHeight = props.Height;

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

		// The polled input answers for this window. GLFW's "the current
		// context" is an OpenGL convention, and a Vulkan window has no
		// context - so without this the engine asked GLFW for a window that
		// does not exist, and every polled key answered "not pressed".
		Input::SetNativeWindow(m_Window);

		// The framebuffer size is the window's size in pixels, which is not
		// the size the window was asked for on a scaled display: a 1600x900
		// window on a 150% display has a 2400x1350 framebuffer. It is read
		// once here and then kept up to date by the callbacks below.
		{
			int fbWidth = 0;
			int fbHeight = 0;
			glfwGetFramebufferSize(m_Window, &fbWidth, &fbHeight);
			if (fbWidth > 0 && fbHeight > 0)
			{
				m_Data.FramebufferWidth = (uint32_t)fbWidth;
				m_Data.FramebufferHeight = (uint32_t)fbHeight;
			}
		}

		// Set up GLFW callbacks
		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			WindowCloseEvent event;
			data.EventCallback(event);
		});

		// The resize event carries both sizes: the window's size in points,
		// which is what the UI lays itself out in, and the framebuffer's
		// size in pixels, which is what is presented. On an unscaled display
		// they are the same number; on a scaled one they are not, and
		// drawing one in the other's units is how the overlay ends up
		// oversized.
		glfwSetWindowSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			data.Width = width;
			data.Height = height;

			int fbWidth = 0;
			int fbHeight = 0;
			glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
			if (fbWidth > 0 && fbHeight > 0)
			{
				data.FramebufferWidth = (uint32_t)fbWidth;
				data.FramebufferHeight = (uint32_t)fbHeight;
			}

			WindowResizeEvent event((uint32_t)width, (uint32_t)height,
				data.FramebufferWidth, data.FramebufferHeight);
			data.EventCallback(event);
		});

		// The framebuffer can change without the window changing: a display
		// with a different scale, or a compositor that changed the scale
		// factor, changes the pixels and not the points. The size callback
		// does not fire for that, so the framebuffer callback carries it.
		glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
		{
			WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
			if (width <= 0 || height <= 0)
				return;  // minimised: there is no framebuffer to size

			data.FramebufferWidth = (uint32_t)width;
			data.FramebufferHeight = (uint32_t)height;

			WindowResizeEvent event(data.Width, data.Height,
				data.FramebufferWidth, data.FramebufferHeight);
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

		// The window is gone: the polled input has nothing to answer for.
		Input::SetNativeWindow(nullptr);

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

		// The pads are read after the events, so the frame's input is the
		// state the pads are in now - not the state they were in when the
		// last frame was drawn.
		PollGamepads();
	}

	void VulkanWindow::PollGamepads()
	{
		for (int pad = 0; pad < kMaxGamepads; ++pad)
		{
			if (!glfwJoystickPresent(pad))
				continue;

			GLFWgamepadstate state{};
			if (!glfwGetGamepadState(pad, &state))
				continue;

			// The axes: a stick that rests a little off centre is not the
			// player doing anything, but the value still has to be reported
			// - the input map's deadzone is the one that decides what counts.
			for (int axis = 0; axis < kMaxAxes && axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis)
			{
				const float value = state.axes[axis];
				if (value == m_LastAxisValues[pad][axis])
					continue;

				m_LastAxisValues[pad][axis] = value;
				JoypadAxisEvent event(pad, axis, value);
				m_Data.EventCallback(event);
			}

			// The buttons.
			for (int button = 0; button < kMaxButtons && button <= GLFW_GAMEPAD_BUTTON_LAST; ++button)
			{
				const bool pressed = state.buttons[button] == GLFW_PRESS;
				if (pressed == m_LastButtonStates[pad][button])
					continue;

				m_LastButtonStates[pad][button] = pressed;
				JoypadButtonEvent event(pad, button, pressed);
				m_Data.EventCallback(event);
			}
		}
	}

	void VulkanWindow::SetVSync(bool enabled)
	{
		// NOTE: glfwSwapInterval only applies to OpenGL/GLES contexts. For a
		// Vulkan surface, vsync is configured through the presentation mode
		// chosen at swapchain creation, so we only record the preference here.
		m_Data.VSync = enabled;
	}

	void VulkanWindow::SetSize(uint32_t width, uint32_t height)
	{
		if (!m_Window || width == 0 || height == 0)
			return;

		// The request is asynchronous: the platform resizes the window and
		// answers with a resize event, and the renderer rebuilds its
		// presentation targets from that event. Nothing here changes the
		// recorded size - a request that the window manager refuses must
		// not leave the engine believing the window is the size that was
		// asked for.
		glfwSetWindowSize(m_Window, (int)width, (int)height);
	}

}

#endif // FG_NO_VULKAN_RUNTIME
