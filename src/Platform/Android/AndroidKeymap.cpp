#include "fgpch.h"

#include "Platform/Android/AndroidKeymap.h"

#include <android/keycodes.h>

namespace Forge {
	namespace Android {

		int TranslateKeyCode(int androidKeyCode)
		{
			// The engine's key codes are GLFW's, so the translation is into
			// GLFW's. The values are spelled out here rather than included
			// from GLFW's header: this file is the only place that knows
			// what a phone's key code means, and it is the only place that
			// has to change when the engine's numbering does.
			constexpr int kUnknown = 0;

			// The letters: Android numbers them from A at 29 upwards, and
			// GLFW's are their own letters' codes.
			if (androidKeyCode >= AKEYCODE_A && androidKeyCode <= AKEYCODE_Z)
				return 'A' + (androidKeyCode - AKEYCODE_A);

			// The digits: Android numbers them from 0 at 7 upwards, and
			// GLFW's are the characters' own codes.
			if (androidKeyCode >= AKEYCODE_0 && androidKeyCode <= AKEYCODE_9)
				return '0' + (androidKeyCode - AKEYCODE_0);

			switch (androidKeyCode)
			{
			case AKEYCODE_SPACE: return 32;   // GLFW_KEY_SPACE
			case AKEYCODE_ENTER: return 257;  // GLFW_KEY_ENTER
			case AKEYCODE_TAB: return 258;    // GLFW_KEY_TAB
			case AKEYCODE_DEL: return 259;        // GLFW_KEY_BACKSPACE
			case AKEYCODE_ESCAPE: return 256; // GLFW_KEY_ESCAPE
			case AKEYCODE_DPAD_UP: return 265;    // GLFW_KEY_UP
			case AKEYCODE_DPAD_DOWN: return 264;  // GLFW_KEY_DOWN
			case AKEYCODE_DPAD_LEFT: return 263;  // GLFW_KEY_LEFT
			case AKEYCODE_DPAD_RIGHT: return 262; // GLFW_KEY_RIGHT
			case AKEYCODE_SHIFT_LEFT: return 340;  // GLFW_KEY_LEFT_SHIFT
			case AKEYCODE_SHIFT_RIGHT: return 344; // GLFW_KEY_RIGHT_SHIFT
			case AKEYCODE_CTRL_LEFT: return 341;  // GLFW_KEY_LEFT_CONTROL
			case AKEYCODE_CTRL_RIGHT: return 345; // GLFW_KEY_RIGHT_CONTROL
			case AKEYCODE_ALT_LEFT: return 342;    // GLFW_KEY_LEFT_ALT
			case AKEYCODE_ALT_RIGHT: return 346;   // GLFW_KEY_RIGHT_ALT
			case AKEYCODE_F1: return 290;
			case AKEYCODE_F2: return 291;
			case AKEYCODE_F3: return 292;
			case AKEYCODE_F4: return 293;
			case AKEYCODE_F5: return 294;
			case AKEYCODE_F6: return 295;
			case AKEYCODE_F7: return 296;
			case AKEYCODE_F8: return 297;
			case AKEYCODE_F9: return 298;
			case AKEYCODE_F10: return 299;
			case AKEYCODE_F11: return 300;
			case AKEYCODE_F12: return 301;
			default: return kUnknown;
			}
		}

	}  // namespace Android
}  // namespace Forge
