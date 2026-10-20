#pragma once

#include "Forge/Core/KeyCodes.h"
#include "Forge/Core/MouseCodes.h"

#include <glm/glm.hpp>

namespace Forge {

	// Input state at the moment it is asked for (the Hazel Input facade).
	//
	// Events are how input arrives - a layer handles KeyPressedEvent and
	// releases - and they remain the right tool for anything that reacts to a
	// transition. This is for the other question gameplay asks every frame:
	// "is this held down?". It reads the window's live state, so it never goes
	// stale and never needs its own bookkeeping.
	//
	// Every function answers for the active window. With no window there is no
	// state to report, so they report nothing rather than reading a null
	// handle - a headless tool gets a false, not a crash.
	class Input
	{
	public:
		// The platform window the queries answer for.
		//
		// The platform layer sets it when it opens a window and clears it
		// when the window goes away. It is the platform's own handle, the
		// same thing `Window::GetNativeWindow` hands out.
		//
		// It has to be set explicitly. GLFW's "the current context" is an
		// OpenGL convention, and a Vulkan window has no context - so asking
		// GLFW for the current context answered "no window" in every
		// application this engine builds, and every polled key answered "not
		// pressed". The camera never moved, and a script's GetKeyDown never
		// fired, in an editor that looked like it was listening.
		static void SetNativeWindow(void* window);

		// The platform's own answer to "is this key down?".
		//
		// A desktop window keeps that state itself and is asked directly.
		// A platform with no window to ask - the phone, where the system's
		// surface is the window - reports it here, and the queries below
		// answer from it.
		static void SetKeyPressed(KeyCode key, bool pressed);
		static void SetMouseButtonPressed(MouseCode button, bool pressed);
		static void SetMousePosition(float x, float y);

		// Whether the platform keeps that state and reports it here.
		//
		// A desktop window keeps the state itself, so this is false there
		// and the window is asked directly. A platform with no window to ask
		// says so, and the queries answer from what it reports.
		//
		// With no window and no platform state, the queries report nothing:
		// a headless tool gets a false, not a stale answer.
		static void SetPlatformReportsState(bool reports);

		[[nodiscard]]
		static bool IsKeyPressed(KeyCode key);

		[[nodiscard]]
		static bool IsKeyReleased(KeyCode key);

		[[nodiscard]]
		static bool IsMouseButtonPressed(MouseCode button);

		[[nodiscard]]
		static bool IsMouseButtonReleased(MouseCode button);

		// Cursor position in window coordinates, origin top-left.
		[[nodiscard]]
		static glm::vec2 GetMousePosition();

		[[nodiscard]]
		static float GetMouseX();

		[[nodiscard]]
		static float GetMouseY();
	};

}  // namespace Forge