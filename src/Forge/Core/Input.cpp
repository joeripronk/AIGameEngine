#include "fgpch.h"
#include "Forge/Core/Base.h"
#include "Forge/Core/Input.h"

#include <set>

namespace Forge {

	namespace {

		// The platform window the queries answer for, set by the platform
		// layer when it opens one.
		void* s_NativeWindow = nullptr;

		// The keys that are down, and the mouse buttons that are down, as
		// the platform reports them.
		//
		// A desktop window answers these questions itself - GLFW keeps the
		// state and GLFW is asked. A platform with no window to ask - the
		// phone, where the system's surface is the window and there is no
		// GLFW at all - has to keep the state itself, and this is it: the
		// events are the state.
		std::set<int> s_PressedKeys;
		std::set<int> s_PressedButtons;

		// Where the pointer is, in window coordinates.
		float s_MouseX = 0.0f;
		float s_MouseY = 0.0f;

		// Whether the platform keeps the state above and reports it here.
		// A desktop window keeps the state itself and is asked directly; a
		// platform with no window to ask reports it here.
		bool s_PlatformReportsState = false;

		// The window the queries answer for.
		//
		// GLFW's polling calls take a window handle, and a null handle means
		// "the current context" - a convention that quietly stops working
		// the moment a second window exists.
		//
		// The current context is also an OpenGL convention: a Vulkan window
		// has no context and never will, so glfwGetCurrentContext() answers
		// null in every Vulkan application - which is every application this
		// engine builds. Asking it for the window therefore meant that every
		// polled key answered "not pressed", in the editor and in the game.
		//
		// The window the input is about is the one the platform layer opened.
#if !defined(FG_PLATFORM_ANDROID)
		GLFWwindow* ActiveWindow()
		{
			if (GLFWwindow* current = glfwGetCurrentContext())
				return current;

			return static_cast<GLFWwindow*>(s_NativeWindow);
		}
#endif

	}  // namespace

	void Input::SetNativeWindow(void* window)
	{
		s_NativeWindow = window;
	}

	void Input::SetPlatformReportsState(bool reports)
	{
		s_PlatformReportsState = reports;
	}

	void Input::SetKeyPressed(KeyCode key, bool pressed)
	{
		if (pressed)
			s_PressedKeys.insert(static_cast<int>(key));
		else
			s_PressedKeys.erase(static_cast<int>(key));
	}

	void Input::SetMouseButtonPressed(MouseCode button, bool pressed)
	{
		if (pressed)
			s_PressedButtons.insert(static_cast<int>(button));
		else
			s_PressedButtons.erase(static_cast<int>(button));
	}

	void Input::SetMousePosition(float x, float y)
	{
		s_MouseX = x;
		s_MouseY = y;
	}

	bool Input::IsKeyPressed(KeyCode key)
	{
#if !defined(FG_PLATFORM_ANDROID)
		if (GLFWwindow* window = ActiveWindow())
			return glfwGetKey(window, key) == GLFW_PRESS;
#endif

		// No window to ask: the state the platform reported is the state -
		// and with no platform state either, there is nothing to report.
		return s_PlatformReportsState && s_PressedKeys.count(static_cast<int>(key)) > 0;
	}

	bool Input::IsKeyReleased(KeyCode key)
	{
#if !defined(FG_PLATFORM_ANDROID)
		if (GLFWwindow* window = ActiveWindow())
			return glfwGetKey(window, key) == GLFW_RELEASE;
#endif

		return s_PlatformReportsState && s_PressedKeys.count(static_cast<int>(key)) == 0;
	}

	bool Input::IsMouseButtonPressed(MouseCode button)
	{
#if !defined(FG_PLATFORM_ANDROID)
		if (GLFWwindow* window = ActiveWindow())
			return glfwGetMouseButton(window, button) == GLFW_PRESS;
#endif

		return s_PlatformReportsState && s_PressedButtons.count(static_cast<int>(button)) > 0;
	}

	bool Input::IsMouseButtonReleased(MouseCode button)
	{
#if !defined(FG_PLATFORM_ANDROID)
		if (GLFWwindow* window = ActiveWindow())
			return glfwGetMouseButton(window, button) == GLFW_RELEASE;
#endif

		return s_PlatformReportsState && s_PressedButtons.count(static_cast<int>(button)) == 0;
	}

	glm::vec2 Input::GetMousePosition()
	{
		double x = s_MouseX, y = s_MouseY;
#if !defined(FG_PLATFORM_ANDROID)
		if (GLFWwindow* window = ActiveWindow())
			glfwGetCursorPos(window, &x, &y);
#endif
		return { static_cast<float>(x), static_cast<float>(y) };
	}

	float Input::GetMouseX()
	{
		return GetMousePosition().x;
	}

	float Input::GetMouseY()
	{
		return GetMousePosition().y;
	}

}  // namespace Forge