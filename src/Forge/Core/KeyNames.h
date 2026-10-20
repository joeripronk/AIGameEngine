#pragma once

#include "Forge/Core/KeyCodes.h"
#include "Forge/Core/MouseCodes.h"

#include <cstdint>
#include <string>

namespace Forge {

	// The names of the keys and the mouse buttons.
	//
	// The codes are numbers - GLFW's, so the platform layer can be handed one
	// without a translation - and a number is not something a person can
	// read in a settings file or in an editor that asks "press a key". These
	// are the two directions of the same table: the name a file holds, and
	// the code the engine works with.
	//
	// A key with no name (a code the platform invented) answers nullptr: the
	// caller writes the number instead, so a binding is never lost to a
	// missing name.
	[[nodiscard]] const char* KeyName(KeyCode key);

	// The code a name stands for. False for a name that is not a key.
	[[nodiscard]] bool KeyFromName(const std::string& name, KeyCode& out);

	[[nodiscard]] const char* MouseButtonName(MouseCode button);
	[[nodiscard]] bool MouseButtonFromName(const std::string& name, MouseCode& out);

}  // namespace Forge
