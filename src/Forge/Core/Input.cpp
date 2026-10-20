#include "fgpch.h"
#include "Forge/Core/Base.h"
#include "Forge/Core/Input.h"

namespace Forge {

	namespace {

		// The platform window the queries answer for, set by the platform
		// layer when it opens one.
		void* s_NativeWindow = nullptr;

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
		GLFWwindow* ActiveWindow()
		{
			if (GLFWwindow* current = glfwGetCurrentContext())
				return current;

			return static_cast<GLFWwindow*>(s_NativeWindow);
		}

	}  // namespace

	void Input::SetNativeWindow(void* window)
	{
		s_NativeWindow = window;
	}

	bool Input::IsKeyPressed(KeyCode key)
	{
		GLFWwindow* window = ActiveWindow();
		return window && glfwGetKey(window, key) == GLFW_PRESS;
	}

	bool Input::IsKeyReleased(KeyCode key)
	{
		GLFWwindow* window = ActiveWindow();
		return window && glfwGetKey(window, key) == GLFW_RELEASE;
	}

	bool Input::IsMouseButtonPressed(MouseCode button)
	{
		GLFWwindow* window = ActiveWindow();
		return window && glfwGetMouseButton(window, button) == GLFW_PRESS;
	}

	bool Input::IsMouseButtonReleased(MouseCode button)
	{
		GLFWwindow* window = ActiveWindow();
		return window && glfwGetMouseButton(window, button) == GLFW_RELEASE;
	}

	glm::vec2 Input::GetMousePosition()
	{
		double x = 0.0, y = 0.0;
		if (GLFWwindow* window = ActiveWindow())
			glfwGetCursorPos(window, &x, &y);
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