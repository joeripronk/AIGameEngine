#pragma once

#include <cstdint>
#include <string>

namespace Forge {

	// The gamepad's buttons and axes, as GLFW names them.
	//
	// The numbers are GLFW's gamepad layout, so the platform layer can hand
	// one straight to GLFW, and the names are what a remap file holds: a
	// player reads "Pad0:A", not "Pad0:0".
	namespace Joypad
	{
		enum Button : uint8_t
		{
			A = 0,
			B,
			X,
			Y,
			LeftBumper,
			RightBumper,
			Back,
			Start,
			Guide,
			LeftThumb,
			RightThumb,
			DPadUp,
			DPadRight,
			DPadDown,
			DPadLeft,

			ButtonCount,
		};

		enum Axis : uint8_t
		{
			LeftX = 0,
			LeftY,
			RightX,
			RightY,
			LeftTrigger,
			RightTrigger,

			AxisCount,
		};

		// The name a button is written under, or nullptr for one the
		// platform invented.
		[[nodiscard]] const char* ButtonName(int button);

		// The button a name stands for. False for a name that is not one.
		[[nodiscard]] bool ButtonFromName(const std::string& name, int& out);

		[[nodiscard]] const char* AxisName(int axis);
		[[nodiscard]] bool AxisFromName(const std::string& name, int& out);
	}

}  // namespace Forge
