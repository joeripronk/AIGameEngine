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