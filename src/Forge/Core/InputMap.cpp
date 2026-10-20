#include "fgpch.h"
#include "Forge/Core/InputMap.h"

#include "Forge/Core/FileSystem.h"
#include "Forge/Core/JoypadCodes.h"
#include "Forge/Core/KeyNames.h"
#include "Forge/Events/JoypadEvent.h"
#include "Forge/Events/KeyEvent.h"
#include "Forge/Events/MouseEvent.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

namespace Forge {

	namespace {

		// The state of one action: the bindings, and whether the action is
		// down. The transient flags are the frame's news - "it went down
		// during this frame" - and are cleared when the frame starts.
		struct ActionState
		{
			std::vector<InputBinding> bindings;
			bool pressed = false;
			bool justPressed = false;
			bool justReleased = false;
		};

		// The map. The actions keep the order they were added in, because a
		// settings screen lists them in that order and a map that reorders
		// itself between two runs is a map the player cannot find anything
		// in.
		std::vector<std::string> s_ActionOrder;
		std::map<std::string, ActionState> s_Actions;

		// The keys and the mouse buttons that are down, and the modifiers
		// among them.
		std::set<uint16_t> s_DownKeys;
		std::set<uint16_t> s_DownMouseButtons;
		uint8_t s_Modifiers = 0;

		// The gamepads: the buttons that are down, and where the axes rest.
		// The key is the pad and the control, so a pad's button and an
		// axis never collide.
		std::set<uint64_t> s_DownPadButtons;
		std::map<uint64_t, float> s_PadAxes;

		// The key a pad's control is remembered under: the pad and the
		// control in one number, so a button and an axis never collide.
		uint64_t PadKey(int joystick, int control)
		{
			return (static_cast<uint64_t>(joystick) << 32) | static_cast<uint64_t>(control);
		}

		uint8_t ModifierBitFor(uint16_t key)
		{
			switch (key)
			{
			case Key::LeftShift:
			case Key::RightShift:
				return static_cast<uint8_t>(InputModifier::Shift);
			case Key::LeftControl:
			case Key::RightControl:
				return static_cast<uint8_t>(InputModifier::Control);
			case Key::LeftAlt:
			case Key::RightAlt:
				return static_cast<uint8_t>(InputModifier::Alt);
			default:
				return 0;
			}
		}

		// The modifiers that are held, recomputed from the keys that are
		// down. Recomputing rather than counting events is what keeps the
		// state right when a key is released while another modifier is
		// still held.
		uint8_t CurrentModifiers()
		{
			uint8_t modifiers = 0;
			for (uint16_t key : s_DownKeys)
				modifiers |= ModifierBitFor(key);
			return modifiers;
		}

		// Does the binding match what is held right now?
		bool BindingIsDown(const InputBinding& binding);

		// How strongly a binding is held: a button is all the way or not at
		// all, and an axis is how far it is past the deadzone.
		float BindingStrength(const InputBinding& binding)
		{
			if (binding.IsJoypadAxis())
			{
				const auto axis = s_PadAxes.find(PadKey(binding.joystick, binding.code));
				if (axis == s_PadAxes.end())
					return 0.0f;

				const float value = std::abs(axis->second);
				if (value < binding.deadzone)
					return 0.0f;

				// The direction decides which way the axis counts: a stick
				// pushed left is the negative action, not a weak positive
				// one.
				if (binding.direction < 0.0f && axis->second > 0.0f)
					return 0.0f;
				if (binding.direction > 0.0f && axis->second < 0.0f)
					return 0.0f;

				return std::min(1.0f, (value - binding.deadzone) / (1.0f - binding.deadzone));
			}

			return BindingIsDown(binding) ? 1.0f : 0.0f;
		}

		bool BindingIsDown(const InputBinding& binding)
		{
			if (binding.IsKey())
			{
				if (s_DownKeys.find(binding.code) == s_DownKeys.end())
					return false;
			}
			else if (binding.IsMouseButton())
			{
				if (s_DownMouseButtons.find(binding.code) == s_DownMouseButtons.end())
					return false;
			}
			else if (binding.IsJoypadButton())
			{
				// The pad the binding names is the one the event came from:
				// a button on another pad is another button.
				if (s_DownPadButtons.find(PadKey(binding.joystick, binding.code)) == s_DownPadButtons.end())
					return false;
			}
			else
			{
				// An axis counts when it is past the deadzone, in the
				// direction the binding names.
				const auto axis = s_PadAxes.find(PadKey(binding.joystick, binding.code));
				if (axis == s_PadAxes.end())
					return false;
				if (std::abs(axis->second) < binding.deadzone)
					return false;
				if (binding.direction > 0 ? axis->second <= 0.0f : axis->second >= 0.0f)
					return false;
			}

			// The modifiers a binding asks for must be among the ones that
			// are held. A binding that asks for none fires whatever else is
			// held down - holding shift must not stop the movement key from
			// working - and a binding that asks for control fires when
			// control is held, with or without shift on top of it.
			//
			// This is Godot's rule, and the stricter one (the modifiers must
			// match exactly) has a defect that shows up in a game: holding
			// shift while the movement key is down would *release* the
			// action, because the binding's modifiers no longer matched the
			// ones held.
			return (binding.modifiers & s_Modifiers) == binding.modifiers;
		}

		// Recomputes what every action is doing, and notes the transitions.
		// The transient flags are only ever set here, never cleared: a
		// second key pressed in the same frame must not take away the first
		// one's "just pressed".
		void RefreshActions()
		{
			s_Modifiers = CurrentModifiers();

			for (auto& [name, state] : s_Actions)
			{
				bool down = false;
				for (const InputBinding& binding : state.bindings)
				{
					if (BindingIsDown(binding))
					{
						down = true;
						break;
					}
				}

				if (down && !state.pressed)
					state.justPressed = true;
				else if (!down && state.pressed)
					state.justReleased = true;

				state.pressed = down;
			}
		}

		const ActionState* FindAction(const std::string& name)
		{
			const auto it = s_Actions.find(name);
			return it == s_Actions.end() ? nullptr : &it->second;
		}

		// The name of a binding as it is written in a remap file.
		std::string BindingToString(const InputBinding& binding)
		{
			std::string result;
			if (binding.modifiers & static_cast<uint8_t>(InputModifier::Control))
				result += "Ctrl+";
			if (binding.modifiers & static_cast<uint8_t>(InputModifier::Shift))
				result += "Shift+";
			if (binding.modifiers & static_cast<uint8_t>(InputModifier::Alt))
				result += "Alt+";

			if (binding.IsJoypadButton())
			{
				result += "Pad" + std::to_string(static_cast<int>(binding.joystick)) + ":";
				const char* name = Joypad::ButtonName(binding.code);
				result += name ? name : std::to_string(static_cast<int>(binding.code));
				return result;
			}

			if (binding.IsJoypadAxis())
			{
				result += "Pad" + std::to_string(static_cast<int>(binding.joystick)) + ":";
				const char* name = Joypad::AxisName(binding.code);
				result += name ? name : std::to_string(static_cast<int>(binding.code));
				// The sign says which way the axis counts.
				result += binding.direction < 0 ? "-" : "+";
				return result;
			}

			const char* name = binding.IsKey()
				? KeyName(static_cast<KeyCode>(binding.code))
				: MouseButtonName(static_cast<MouseCode>(binding.code));

			if (name)
			{
				result += name;
			}
			else
			{
				// A code the name table does not know is written as its
				// number: a binding is never lost to a missing name.
				result += std::to_string(static_cast<int>(binding.code));
			}

			return result;
		}

		// The binding a file's text stands for. False when the text names
		// nothing the engine knows.
		bool BindingFromString(const std::string& text, InputBinding& out)
		{
			uint8_t modifiers = 0;
			size_t start = 0;

			// The modifiers, each of them followed by a '+'.
			while (true)
			{
				const size_t plus = text.find('+', start);
				if (plus == std::string::npos)
					break;

				const std::string modifier = text.substr(start, plus - start);
				if (modifier == "Ctrl")
					modifiers |= static_cast<uint8_t>(InputModifier::Control);
				else if (modifier == "Shift")
					modifiers |= static_cast<uint8_t>(InputModifier::Shift);
				else if (modifier == "Alt")
					modifiers |= static_cast<uint8_t>(InputModifier::Alt);
				else
				{
					// Not a modifier: the '+' belongs to the name. A gamepad
					// axis is written "Pad0:LeftX+", and the sign says which
					// way the axis counts.
					break;
				}

				start = plus + 1;
			}

			const std::string name = text.substr(start);
			if (name.empty())
				return false;

			// A gamepad binding: "Pad0:A" for a button, "Pad0:LeftX+" for
			// an axis and the way it counts. The pad's number is between
			// the "Pad" and the colon, so it can be more than one digit.
			const size_t colon = name.find(':');
			if (name.size() > 4 && name.compare(0, 3, "Pad") == 0 && colon != std::string::npos && colon > 3)
			{
				const std::string pad = name.substr(3, colon - 3);
				const std::string control = name.substr(colon + 1);

				int joystick = 0;
				try
				{
					joystick = std::stoi(pad);
				}
				catch (const std::exception&)
				{
					return false;
				}

				// A trailing '+' or '-' says which way an axis counts.
				if (!control.empty() && (control.back() == '+' || control.back() == '-'))
				{
					const int direction = control.back() == '-' ? -1 : 1;
					const std::string axisName = control.substr(0, control.size() - 1);
					int axis = 0;
					if (!Joypad::AxisFromName(axisName, axis))
						return false;
					out.kind = InputBinding::Kind::JoypadAxis;
					out.code = static_cast<uint16_t>(axis);
					out.joystick = static_cast<int8_t>(joystick);
					out.direction = static_cast<int8_t>(direction);
					return true;
				}

				int button = 0;
				if (!Joypad::ButtonFromName(control, button))
					return false;
				out.kind = InputBinding::Kind::JoypadButton;
				out.code = static_cast<uint16_t>(button);
				out.joystick = static_cast<int8_t>(joystick);
				return true;
			}

			KeyCode key = 0;
			if (KeyFromName(name, key))
			{
				out.kind = InputBinding::Kind::Key;
				out.code = key;
				out.modifiers = modifiers;
				return true;
			}

			MouseCode button = 0;
			if (MouseButtonFromName(name, button))
			{
				out.kind = InputBinding::Kind::MouseButton;
				out.code = button;
				out.modifiers = modifiers;
				return true;
			}

			// A bare number is a code the name table does not know.
			if (name.size() > 0 && name.find_first_not_of("0123456789") == std::string::npos)
			{
				out.kind = InputBinding::Kind::Key;
				out.code = static_cast<uint16_t>(std::stoi(name));
				out.modifiers = modifiers;
				return true;
			}

			return false;
		}

		std::string Trim(const std::string& text)
		{
			const size_t first = text.find_first_not_of(" \t\r\n");
			if (first == std::string::npos)
				return {};
			const size_t last = text.find_last_not_of(" \t\r\n");
			return text.substr(first, last - first + 1);
		}

	}  // namespace

	std::string InputBinding::ToString() const
	{
		return BindingToString(*this);
	}

	bool InputMap::AddAction(const std::string& name)
	{
		if (name.empty() || s_Actions.find(name) != s_Actions.end())
			return false;

		s_Actions.emplace(name, ActionState{});
		s_ActionOrder.push_back(name);
		return true;
	}

	bool InputMap::RemoveAction(const std::string& name)
	{
		const auto it = s_Actions.find(name);
		if (it == s_Actions.end())
			return false;

		s_Actions.erase(it);
		s_ActionOrder.erase(std::remove(s_ActionOrder.begin(), s_ActionOrder.end(), name), s_ActionOrder.end());
		return true;
	}

	bool InputMap::HasAction(const std::string& name)
	{
		return s_Actions.find(name) != s_Actions.end();
	}

	std::vector<std::string> InputMap::GetActions()
	{
		return s_ActionOrder;
	}

	bool InputMap::AddKey(const std::string& action, KeyCode key, uint8_t modifiers)
	{
		const auto it = s_Actions.find(action);
		if (it == s_Actions.end())
			return false;

		InputBinding binding;
		binding.kind = InputBinding::Kind::Key;
		binding.code = key;
		binding.modifiers = modifiers;

		for (const InputBinding& existing : it->second.bindings)
		{
			if (existing.kind == binding.kind && existing.code == binding.code && existing.modifiers == binding.modifiers)
				return false;  // already bound
		}

		it->second.bindings.push_back(binding);
		RefreshActions();
		return true;
	}

	bool InputMap::AddMouseButton(const std::string& action, MouseCode button, uint8_t modifiers)
	{
		const auto it = s_Actions.find(action);
		if (it == s_Actions.end())
			return false;

		InputBinding binding;
		binding.kind = InputBinding::Kind::MouseButton;
		binding.code = button;
		binding.modifiers = modifiers;

		for (const InputBinding& existing : it->second.bindings)
		{
			if (existing.kind == binding.kind && existing.code == binding.code && existing.modifiers == binding.modifiers)
				return false;
		}

		it->second.bindings.push_back(binding);
		RefreshActions();
		return true;
	}

	bool InputMap::AddJoypadButton(const std::string& action, int joystick, int button)
	{
		const auto it = s_Actions.find(action);
		if (it == s_Actions.end())
			return false;

		InputBinding binding;
		binding.kind = InputBinding::Kind::JoypadButton;
		binding.code = static_cast<uint16_t>(button);
		binding.joystick = static_cast<int8_t>(joystick);

		for (const InputBinding& existing : it->second.bindings)
		{
			if (existing.kind == binding.kind && existing.code == binding.code &&
				existing.joystick == binding.joystick)
			{
				return false;
			}
		}

		it->second.bindings.push_back(binding);
		RefreshActions();
		return true;
	}

	bool InputMap::AddJoypadAxis(const std::string& action, int joystick, int axis,
		int direction, float deadzone)
	{
		const auto it = s_Actions.find(action);
		if (it == s_Actions.end())
			return false;

		InputBinding binding;
		binding.kind = InputBinding::Kind::JoypadAxis;
		binding.code = static_cast<uint16_t>(axis);
		binding.joystick = static_cast<int8_t>(joystick);
		binding.direction = direction < 0 ? -1 : 1;
		binding.deadzone = std::clamp(deadzone, 0.0f, 0.9f);

		for (const InputBinding& existing : it->second.bindings)
		{
			if (existing.kind == binding.kind && existing.code == binding.code &&
				existing.joystick == binding.joystick && existing.direction == binding.direction)
			{
				return false;
			}
		}

		it->second.bindings.push_back(binding);
		RefreshActions();
		return true;
	}

	bool InputMap::RemoveBinding(const std::string& action, uint16_t code)
	{
		const auto it = s_Actions.find(action);
		if (it == s_Actions.end())
			return false;

		const size_t before = it->second.bindings.size();
		it->second.bindings.erase(
			std::remove_if(it->second.bindings.begin(), it->second.bindings.end(),
				[code](const InputBinding& binding) { return binding.code == code; }),
			it->second.bindings.end());

		if (it->second.bindings.size() == before)
			return false;

		RefreshActions();
		return true;
	}

	std::vector<InputBinding> InputMap::GetBindings(const std::string& action)
	{
		const ActionState* state = FindAction(action);
		if (!state)
			return {};
		return state->bindings;
	}

	std::vector<std::string> InputMap::GetActionsForKey(KeyCode key)
	{
		std::vector<std::string> actions;
		for (const std::string& name : s_ActionOrder)
		{
			const ActionState* state = FindAction(name);
			if (!state)
				continue;

			for (const InputBinding& binding : state->bindings)
			{
				if (!binding.IsKey() || binding.code != key)
					continue;

				// The modifiers the binding asks for must be held, the
				// same rule the state uses.
				if ((binding.modifiers & s_Modifiers) == binding.modifiers)
				{
					actions.push_back(name);
					break;
				}
			}
		}
		return actions;
	}

	std::vector<std::string> InputMap::GetActionsForMouseButton(MouseCode button)
	{
		std::vector<std::string> actions;
		for (const std::string& name : s_ActionOrder)
		{
			const ActionState* state = FindAction(name);
			if (!state)
				continue;

			for (const InputBinding& binding : state->bindings)
			{
				if (binding.IsKey() || binding.code != button)
					continue;

				if ((binding.modifiers & s_Modifiers) == binding.modifiers)
				{
					actions.push_back(name);
					break;
				}
			}
		}
		return actions;
	}

	bool InputMap::IsActionPressed(const std::string& action)
	{
		const ActionState* state = FindAction(action);
		return state && state->pressed;
	}

	bool InputMap::IsActionJustPressed(const std::string& action)
	{
		const ActionState* state = FindAction(action);
		return state && state->justPressed;
	}

	bool InputMap::IsActionJustReleased(const std::string& action)
	{
		const ActionState* state = FindAction(action);
		return state && state->justReleased;
	}

	float InputMap::GetActionStrength(const std::string& action)
	{
		const ActionState* state = FindAction(action);
		if (!state)
			return 0.0f;

		// The strongest binding wins: an action bound to a key and to a
		// stick is held as hard as the harder of the two.
		float strength = 0.0f;
		for (const InputBinding& binding : state->bindings)
			strength = std::max(strength, BindingStrength(binding));
		return strength;
	}

	float InputMap::GetAxis(const std::string& negativeAction, const std::string& positiveAction)
	{
		const float negative = IsActionPressed(negativeAction) ? 1.0f : 0.0f;
		const float positive = IsActionPressed(positiveAction) ? 1.0f : 0.0f;
		return positive - negative;
	}

	void InputMap::OnEvent(const Event& event)
	{
		switch (event.GetEventType())
		{
		case EventType::KeyPressed:
		{
			const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
			// A repeat is not a new press: the key was already down, and an
			// action that fires on the repeat would fire many times for one
			// press.
			if (keyEvent.GetRepeatCount() == 0)
				s_DownKeys.insert(static_cast<uint16_t>(keyEvent.GetKeyCode()));
			break;
		}
		case EventType::KeyReleased:
		{
			const auto& keyEvent = static_cast<const KeyReleasedEvent&>(event);
			s_DownKeys.erase(static_cast<uint16_t>(keyEvent.GetKeyCode()));
			break;
		}
		case EventType::JoypadButton:
		{
			const auto& padEvent = static_cast<const JoypadButtonEvent&>(event);
			const uint64_t key = PadKey(padEvent.GetJoystick(), padEvent.GetButton());
			if (padEvent.IsPressed())
				s_DownPadButtons.insert(key);
			else
				s_DownPadButtons.erase(key);
			break;
		}
		case EventType::JoypadAxis:
		{
			const auto& padEvent = static_cast<const JoypadAxisEvent&>(event);
			s_PadAxes[PadKey(padEvent.GetJoystick(), padEvent.GetAxis())] = padEvent.GetValue();
			break;
		}
		case EventType::MouseButtonPressed:
		{
			const auto& mouseEvent = static_cast<const MouseButtonPressedEvent&>(event);
			s_DownMouseButtons.insert(static_cast<uint16_t>(mouseEvent.GetMouseButton()));
			break;
		}
		case EventType::MouseButtonReleased:
		{
			const auto& mouseEvent = static_cast<const MouseButtonReleasedEvent&>(event);
			s_DownMouseButtons.erase(static_cast<uint16_t>(mouseEvent.GetMouseButton()));
			break;
		}
		default:
			return;
		}

		RefreshActions();
	}

	void InputMap::BeginFrame()
	{
		for (auto& [name, state] : s_Actions)
		{
			state.justPressed = false;
			state.justReleased = false;
		}
	}

	bool InputMap::SaveToFile(const std::string& path)
	{
		std::ostringstream out;
		out << "# Forge input map\n";
		out << "# One action per line: the name, then the bindings separated by commas.\n";
		out << "# A binding may ask for modifiers: Ctrl+Shift+A.\n";

		for (const std::string& name : s_ActionOrder)
		{
			const ActionState* state = FindAction(name);
			if (!state)
				continue;

			out << name << " =";
			for (const InputBinding& binding : state->bindings)
				out << " " << BindingToString(binding);
			out << "\n";
		}

		return FileSystem::WriteTextFile(path, out.str());
	}

	bool InputMap::LoadFromFile(const std::string& path)
	{
		std::string text;
		if (!FileSystem::ReadTextFile(path, text))
			return false;

		// The map is only changed once the whole file has been read: a file
		// that cannot be parsed leaves the map exactly as it was, because
		// half a map is worse than the one that was there.
		std::vector<std::pair<std::string, std::vector<InputBinding>>> parsed;

		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			const std::string trimmed = Trim(line);
			if (trimmed.empty() || trimmed[0] == '#')
				continue;

			const size_t equals = trimmed.find('=');
			if (equals == std::string::npos)
				continue;  // a line that is not an action: ignored

			const std::string name = Trim(trimmed.substr(0, equals));
			if (name.empty())
				continue;

			std::vector<InputBinding> bindings;
			std::string rest = trimmed.substr(equals + 1);
			std::replace(rest.begin(), rest.end(), ',', ' ');

			std::istringstream bindingStream(rest);
			std::string bindingText;
			while (bindingStream >> bindingText)
			{
				InputBinding binding;
				if (BindingFromString(bindingText, binding))
					bindings.push_back(binding);
				else
					FG_CORE_WARN("InputMap: could not read the binding '{0}'", bindingText);
			}

			parsed.emplace_back(name, bindings);
		}

		for (const auto& [name, bindings] : parsed)
		{
			// The action may already be there - a remap file that adds a
			// binding to an action the game has is the normal case.
			(void)InputMap::AddAction(name);

			ActionState* state = nullptr;
			const auto it = s_Actions.find(name);
			if (it != s_Actions.end())
				state = &it->second;

			if (!state)
				continue;

			// The bindings in the file replace the ones the action had: a
			// remap file is the whole truth about that action.
			state->bindings = bindings;
		}

		RefreshActions();
		return true;
	}

	void InputMap::Clear()
	{
		s_Actions.clear();
		s_ActionOrder.clear();
		s_DownKeys.clear();
		s_DownMouseButtons.clear();
		s_DownPadButtons.clear();
		s_PadAxes.clear();
		s_Modifiers = 0;
	}

}  // namespace Forge
