#pragma once

#include "Forge/Core/KeyCodes.h"
#include "Forge/Core/MouseCodes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Forge {

	class Event;

	// The modifiers a binding may require. The bits are a set, so a binding
	// can ask for "shift and control" as well as for one of them.
	enum class InputModifier : uint8_t
	{
		None    = 0,
		Shift   = 1 << 0,
		Control = 1 << 1,
		Alt     = 1 << 2,
	};

	// One binding of an action: a key or a mouse button, and the modifiers
	// it requires.
	//
	// The modifiers a binding asks for must be among the ones held, which
	// is Godot's rule: a binding that asks for none fires whatever else is
	// held down, and a binding that asks for control fires when control is
	// held, with or without shift on top of it.
	struct InputBinding
	{
		enum class Kind : uint8_t { Key, MouseButton };

		Kind kind = Kind::Key;
		uint16_t code = 0;     // a KeyCode or a MouseCode
		uint8_t modifiers = 0; // the InputModifier bits

		[[nodiscard]] bool IsKey() const { return kind == Kind::Key; }
		[[nodiscard]] bool IsMouseButton() const { return kind == Kind::MouseButton; }

		// The binding's name as it is written in a remap file:
		// "Ctrl+Shift+A", "MouseLeft".
		[[nodiscard]] std::string ToString() const;
	};

	// The input action map (the Godot InputMap model).
	//
	// Gameplay asks for an action by name - "move_left", "jump", "fire" -
	// and never for a key code. The binding between the name and the key is
	// data: it lives in the map, the map is saved with the game, and the
	// player can change it without the game being recompiled.
	//
	// The state comes from the events, not from polling: a press *is* an
	// event, and "just pressed" is a transition rather than a comparison
	// with the previous frame. That makes the answers exact and makes them
	// testable with no window at all.
	class InputMap
	{
	public:
		// --- The actions -----------------------------------------------------

		// An action with no bindings is a name and nothing else: the map
		// accepts it, and a binding can be added later.
		[[nodiscard]] static bool AddAction(const std::string& name);

		// False for an action that was not there.
		[[nodiscard]] static bool RemoveAction(const std::string& name);

		[[nodiscard]] static bool HasAction(const std::string& name);

		// The action names, in the order they were added.
		[[nodiscard]] static std::vector<std::string> GetActions();

		// --- The bindings ----------------------------------------------------

		// False when the action does not exist, or when the binding is
		// already there: a binding that is added twice would fire twice.
		[[nodiscard]] static bool AddKey(const std::string& action, KeyCode key, uint8_t modifiers = 0);
		[[nodiscard]] static bool AddMouseButton(const std::string& action, MouseCode button, uint8_t modifiers = 0);

		// Removes every binding of the action that uses the code, whatever
		// its modifiers. False when there was none.
		[[nodiscard]] static bool RemoveBinding(const std::string& action, uint16_t code);

		// The bindings of an action, or an empty list for an action that
		// does not exist.
		[[nodiscard]] static std::vector<InputBinding> GetBindings(const std::string& action);

		// The actions a key is bound to. The other direction of the map, and
		// the one a game that reacts to key events asks: "what does this key
		// mean?".
		//
		// The modifiers are the ones held right now: a binding that asks for
		// control is only among the answers while control is held.
		[[nodiscard]] static std::vector<std::string> GetActionsForKey(KeyCode key);
		[[nodiscard]] static std::vector<std::string> GetActionsForMouseButton(MouseCode button);

		// --- The state -------------------------------------------------------

		// Is the action's binding held down?
		[[nodiscard]] static bool IsActionPressed(const std::string& action);

		// Did the action go down during this frame? True for the frame the
		// binding was pressed and false after it.
		[[nodiscard]] static bool IsActionJustPressed(const std::string& action);

		// Did the action come up during this frame?
		[[nodiscard]] static bool IsActionJustReleased(const std::string& action);

		// How strongly the action is pressed: 1 when it is, 0 when it is
		// not. The analogue actions Godot has (a stick, a trigger) are not
		// in the map, so the answer is a step.
		[[nodiscard]] static float GetActionStrength(const std::string& action);

		// The axis two actions make: the positive one gives 1, the negative
		// one gives -1, both give 0. This is how a movement is read:
		// GetAxis("move_left", "move_right").
		[[nodiscard]] static float GetAxis(const std::string& negativeAction, const std::string& positiveAction);

		// --- The events ------------------------------------------------------

		// Feeds the map an event. The application calls it for every event
		// it dispatches.
		static void OnEvent(const Event& event);

		// The frame boundary: the transient state ("just pressed", "just
		// released") is cleared here, so it describes the frame that is
		// starting and not the one that ended.
		static void BeginFrame();

		// --- The map on disk -------------------------------------------------

		// The map as a text file, one action per line:
		//
		//     move_left = A, Left
		//     jump = Space
		//     fire = MouseLeft
		//     zoom = Ctrl+Equal
		//
		// A binding that needs a modifier the key table has no name for is
		// written with its number, so nothing is lost.
		[[nodiscard]] static bool SaveToFile(const std::string& path);

		// Reads a map file. An action the file does not mention keeps the
		// bindings it had; an action in the file that the map does not have
		// is added. False when the file cannot be read - and then the map is
		// exactly what it was, because half a map is worse than the one
		// that was there.
		[[nodiscard]] static bool LoadFromFile(const std::string& path);

		// --- The whole map ---------------------------------------------------

		// Forgets every action and every key the map was holding. A new
		// project starts with an empty map.
		static void Clear();
	};

}  // namespace Forge
