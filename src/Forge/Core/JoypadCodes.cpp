#include "fgpch.h"
#include "Forge/Core/JoypadCodes.h"

namespace Forge {

	namespace Joypad {

		namespace {

			struct NamedControl
			{
				const char* name;
				int control;
			};

			// The names the controls are written under in a remap file. The
			// list is the gamepad layout's, so a binding reads the same on
			// every pad.
			const NamedControl kButtons[] = {
				{ "A", A },
				{ "B", B },
				{ "X", X },
				{ "Y", Y },
				{ "LeftBumper", LeftBumper },
				{ "RightBumper", RightBumper },
				{ "Back", Back },
				{ "Start", Start },
				{ "Guide", Guide },
				{ "LeftThumb", LeftThumb },
				{ "RightThumb", RightThumb },
				{ "DPadUp", DPadUp },
				{ "DPadRight", DPadRight },
				{ "DPadDown", DPadDown },
				{ "DPadLeft", DPadLeft },
			};

			const NamedControl kAxes[] = {
				{ "LeftX", LeftX },
				{ "LeftY", LeftY },
				{ "RightX", RightX },
				{ "RightY", RightY },
				{ "LeftTrigger", LeftTrigger },
				{ "RightTrigger", RightTrigger },
			};

			const char* FindName(const NamedControl* table, size_t count, int control)
			{
				for (size_t i = 0; i < count; ++i)
				{
					if (table[i].control == control)
						return table[i].name;
				}
				return nullptr;
			}

			bool FindControl(const NamedControl* table, size_t count, const std::string& name, int& out)
			{
				for (size_t i = 0; i < count; ++i)
				{
					if (name == table[i].name)
					{
						out = table[i].control;
						return true;
					}
				}
				return false;
			}

		}  // namespace

		const char* ButtonName(int button)
		{
			return FindName(kButtons, sizeof(kButtons) / sizeof(kButtons[0]), button);
		}

		bool ButtonFromName(const std::string& name, int& out)
		{
			return FindControl(kButtons, sizeof(kButtons) / sizeof(kButtons[0]), name, out);
		}

		const char* AxisName(int axis)
		{
			return FindName(kAxes, sizeof(kAxes) / sizeof(kAxes[0]), axis);
		}

		bool AxisFromName(const std::string& name, int& out)
		{
			return FindControl(kAxes, sizeof(kAxes) / sizeof(kAxes[0]), name, out);
		}

	}  // namespace Joypad

}  // namespace Forge
