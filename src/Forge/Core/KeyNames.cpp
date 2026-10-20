#include "fgpch.h"
#include "Forge/Core/KeyNames.h"

#include <cstring>

namespace Forge {

	namespace {

		struct NamedCode
		{
			const char* name;
			uint16_t code;
		};

		// The table is written once, in the order of the enum, and both
		// directions read it: a name that is not in the table is not a key,
		// and a code that is not in the table has no name.
		const NamedCode kNamedKeys[] = {
			{ "Space", Key::Space },
			{ "Apostrophe", Key::Apostrophe },
			{ "Comma", Key::Comma },
			{ "Minus", Key::Minus },
			{ "Period", Key::Period },
			{ "Slash", Key::Slash },
			{ "Semicolon", Key::Semicolon },
			{ "Equal", Key::Equal },
			{ "LeftBracket", Key::LeftBracket },
			{ "Backslash", Key::Backslash },
			{ "RightBracket", Key::RightBracket },
			{ "GraveAccent", Key::GraveAccent },
			{ "D0", Key::D0 },
			{ "D1", Key::D1 },
			{ "D2", Key::D2 },
			{ "D3", Key::D3 },
			{ "D4", Key::D4 },
			{ "D5", Key::D5 },
			{ "D6", Key::D6 },
			{ "D7", Key::D7 },
			{ "D8", Key::D8 },
			{ "D9", Key::D9 },
			{ "A", Key::A },
			{ "B", Key::B },
			{ "C", Key::C },
			{ "D", Key::D },
			{ "E", Key::E },
			{ "F", Key::F },
			{ "G", Key::G },
			{ "H", Key::H },
			{ "I", Key::I },
			{ "J", Key::J },
			{ "K", Key::K },
			{ "L", Key::L },
			{ "M", Key::M },
			{ "N", Key::N },
			{ "O", Key::O },
			{ "P", Key::P },
			{ "Q", Key::Q },
			{ "R", Key::R },
			{ "S", Key::S },
			{ "T", Key::T },
			{ "U", Key::U },
			{ "V", Key::V },
			{ "W", Key::W },
			{ "X", Key::X },
			{ "Y", Key::Y },
			{ "Z", Key::Z },
			{ "World1", Key::World1 },
			{ "World2", Key::World2 },
			{ "F1", Key::F1 },
			{ "F2", Key::F2 },
			{ "F3", Key::F3 },
			{ "F4", Key::F4 },
			{ "F5", Key::F5 },
			{ "F6", Key::F6 },
			{ "F7", Key::F7 },
			{ "F8", Key::F8 },
			{ "F9", Key::F9 },
			{ "F10", Key::F10 },
			{ "F11", Key::F11 },
			{ "F12", Key::F12 },
			{ "F13", Key::F13 },
			{ "F14", Key::F14 },
			{ "F15", Key::F15 },
			{ "F16", Key::F16 },
			{ "F17", Key::F17 },
			{ "F18", Key::F18 },
			{ "F19", Key::F19 },
			{ "F20", Key::F20 },
			{ "F21", Key::F21 },
			{ "F22", Key::F22 },
			{ "F23", Key::F23 },
			{ "F24", Key::F24 },
			{ "F25", Key::F25 },
			{ "Escape", Key::Escape },
			{ "Enter", Key::Enter },
			{ "Tab", Key::Tab },
			{ "Backspace", Key::Backspace },
			{ "Insert", Key::Insert },
			{ "Delete", Key::Delete },
			{ "Right", Key::Right },
			{ "Left", Key::Left },
			{ "Down", Key::Down },
			{ "Up", Key::Up },
			{ "PageUp", Key::PageUp },
			{ "PageDown", Key::PageDown },
			{ "Home", Key::Home },
			{ "End", Key::End },
			{ "CapsLock", Key::CapsLock },
			{ "ScrollLock", Key::ScrollLock },
			{ "NumLock", Key::NumLock },
			{ "PrintScreen", Key::PrintScreen },
			{ "Pause", Key::Pause },
			{ "Menu", Key::Menu },
			{ "Kp0", Key::Kp0 },
			{ "Kp1", Key::Kp1 },
			{ "Kp2", Key::Kp2 },
			{ "Kp3", Key::Kp3 },
			{ "Kp4", Key::Kp4 },
			{ "Kp5", Key::Kp5 },
			{ "Kp6", Key::Kp6 },
			{ "Kp7", Key::Kp7 },
			{ "Kp8", Key::Kp8 },
			{ "Kp9", Key::Kp9 },
			{ "KpDecimal", Key::KpDecimal },
			{ "KpDivide", Key::KpDivide },
			{ "KpMultiply", Key::KpMultiply },
			{ "KpSubtract", Key::KpSubtract },
			{ "KpAdd", Key::KpAdd },
			{ "KpEnter", Key::KpEnter },
			{ "KpEqual", Key::KpEqual },
			{ "LeftShift", Key::LeftShift },
			{ "LeftControl", Key::LeftControl },
			{ "LeftAlt", Key::LeftAlt },
			{ "LeftSuper", Key::LeftSuper },
			{ "RightShift", Key::RightShift },
			{ "RightControl", Key::RightControl },
			{ "RightAlt", Key::RightAlt },
			{ "RightSuper", Key::RightSuper },
		};

		const NamedCode kNamedMouseButtons[] = {
			{ "MouseLeft", Mouse::ButtonLeft },
			{ "MouseRight", Mouse::ButtonRight },
			{ "MouseMiddle", Mouse::ButtonMiddle },
			{ "MouseButton4", Mouse::Button3 },
			{ "MouseButton5", Mouse::Button4 },
			{ "MouseButton6", Mouse::Button5 },
			{ "MouseButton7", Mouse::Button6 },
			{ "MouseButton8", Mouse::Button7 },
		};

		const char* FindName(const NamedCode* table, size_t count, uint16_t code)
		{
			for (size_t i = 0; i < count; ++i)
			{
				if (table[i].code == code)
					return table[i].name;
			}
			return nullptr;
		}

		bool FindCode(const NamedCode* table, size_t count, const std::string& name, uint16_t& out)
		{
			for (size_t i = 0; i < count; ++i)
			{
				if (name == table[i].name)
				{
					out = table[i].code;
					return true;
				}
			}
			return false;
		}

	}  // namespace

	const char* KeyName(KeyCode key)
	{
		return FindName(kNamedKeys, sizeof(kNamedKeys) / sizeof(kNamedKeys[0]), key);
	}

	bool KeyFromName(const std::string& name, KeyCode& out)
	{
		uint16_t code = 0;
		if (!FindCode(kNamedKeys, sizeof(kNamedKeys) / sizeof(kNamedKeys[0]), name, code))
			return false;
		out = static_cast<KeyCode>(code);
		return true;
	}

	const char* MouseButtonName(MouseCode button)
	{
		return FindName(kNamedMouseButtons, sizeof(kNamedMouseButtons) / sizeof(kNamedMouseButtons[0]), button);
	}

	bool MouseButtonFromName(const std::string& name, MouseCode& out)
	{
		uint16_t code = 0;
		if (!FindCode(kNamedMouseButtons, sizeof(kNamedMouseButtons) / sizeof(kNamedMouseButtons[0]), name, code))
			return false;
		out = static_cast<MouseCode>(code);
		return true;
	}

}  // namespace Forge
