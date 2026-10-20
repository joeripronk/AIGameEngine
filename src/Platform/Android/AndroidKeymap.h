#pragma once

namespace Forge {
	namespace Android {

		// The engine's key codes are GLFW's - one table, one set of numbers,
		// for every platform. Android numbers its keys its own way, so the
		// platform's key is translated here.
		//
		// The answer is a GLFW key code, or 0 for a key the engine does not
		// name: a key nobody asked for is a key that does nothing, which is
		// the same answer a desktop gives for a key it does not know.
		int TranslateKeyCode(int androidKeyCode);

	}  // namespace Android
}  // namespace Forge
