#pragma once

#include <cstdint>

namespace Forge {

	// Mouse button codes are GLFW's, for the same reason KeyCode is: the
	// values are handed straight to the platform layer and asserted against
	// GLFW_MOUSE_BUTTON_* in KeyCodesTests.
	using MouseCode = uint16_t;

	namespace Mouse
	{
		enum : MouseCode
		{
			Button0      = 0,
			Button1      = 1,
			Button2      = 2,
			Button3      = 3,
			Button4      = 4,
			Button5      = 5,
			Button6      = 6,
			Button7      = 7,

			ButtonLast   = Button7,

			ButtonLeft   = Button0,
			ButtonRight  = Button1,
			ButtonMiddle = Button2
		};
	}

}  // namespace Forge