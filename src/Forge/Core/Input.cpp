#include "fgpch.h"
#include "Forge/Core/Base.h"
#include "Forge/Core/Input.h"

namespace Forge {

	namespace {

		// The window the queries answer for. GLFW's polling calls take a
		// window handle, and a null handle means "the current context" - a
		// convention that quietly stops working the moment a second window
		// exists, so the handle is resolved here instead and the current
		// context is checked before it is used.
		GLFWwindow* ActiveWindow()
		{
			return glfwGetCurrentContext();
		}

	}  // namespace

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