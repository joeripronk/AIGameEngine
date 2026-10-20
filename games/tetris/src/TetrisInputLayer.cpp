#include "fgpch.h"

#include "TetrisInputLayer.h"

#include "Forge/Core/InputMap.h"
#include "Forge/Core/KeyCodes.h"
#include "Forge/Events/KeyEvent.h"

namespace Tetris {

	using namespace Forge;

	namespace {

		// The default bindings: the actions the game listens for, and the
		// keys they answer to.
		//
		// Two keys on one action are two ways of doing the same thing - the
		// arrow keys and the WASD keys - which is what a player expects and
		// what the map is for: the binding is data, so a second key is one
		// more line here and not another branch in the input handling.
		struct DefaultBinding
		{
			const char* action;
			KeyCode key;
		};

		const DefaultBinding kDefaultBindings[] = {
			{ InputAction::MoveLeft, Key::Left },
			{ InputAction::MoveLeft, Key::A },
			{ InputAction::MoveRight, Key::Right },
			{ InputAction::MoveRight, Key::D },
			{ InputAction::SoftDrop, Key::Down },
			{ InputAction::SoftDrop, Key::S },
			{ InputAction::HardDrop, Key::Space },
			{ InputAction::RotateCW, Key::Up },
			{ InputAction::RotateCW, Key::W },
			{ InputAction::RotateCW, Key::X },
			{ InputAction::RotateCCW, Key::Z },
			{ InputAction::TogglePause, Key::P },
			{ InputAction::TogglePause, Key::Escape },
			{ InputAction::Restart, Key::R },
			{ InputAction::ToggleView, Key::V },
			{ InputAction::ToggleMusic, Key::M },
			{ InputAction::MusicNext, Key::N },
			{ InputAction::MusicPrevious, Key::B },
		};

	}  // namespace

	bool SetupInputMap()
	{
		bool ok = true;

		for (const DefaultBinding& binding : kDefaultBindings)
		{
			// The action is created the first time it is seen, and an
			// action that is already there keeps the bindings it has: this
			// is called again after a remap file has been loaded, and a
			// default binding must not undo the player's own.
			if (!InputMap::HasAction(binding.action))
				ok = InputMap::AddAction(binding.action) && ok;

			// A binding that is already there is not an error either: the
			// default key may be one of the keys the player has bound.
			(void)InputMap::AddKey(binding.action, binding.key);
		}

		return ok;
	}

	Action RuleActionFor(const std::string& inputAction)
	{
		if (inputAction == InputAction::MoveLeft)
			return Action::MoveLeft;
		if (inputAction == InputAction::MoveRight)
			return Action::MoveRight;
		if (inputAction == InputAction::SoftDrop)
			return Action::SoftDrop;
		if (inputAction == InputAction::HardDrop)
			return Action::HardDrop;
		if (inputAction == InputAction::RotateCW)
			return Action::RotateCW;
		if (inputAction == InputAction::RotateCCW)
			return Action::RotateCCW;
		if (inputAction == InputAction::TogglePause)
			return Action::TogglePause;
		if (inputAction == InputAction::Restart)
			return Action::Restart;

		// The presentation actions are not rules: they have no Action.
		return Action::None;
	}

	void InputLayer::OnEvent(Forge::Event& event)
	{
		using namespace Forge;

		if (KeyPressedEvent* pressed = dynamic_cast<KeyPressedEvent*>(&event))
		{
			// A repeat is the window's own autorepeat, not a new press. Acting on it
			// would move twice per tick on top of the manager's delayed repeat, which
			// is exactly the double-step DAS exists to avoid.
			if (pressed->GetRepeatCount() != 0)
				return;

			// What the key means is what the input map says it means.
			for (const std::string& action : InputMap::GetActionsForKey(static_cast<KeyCode>(pressed->GetKeyCode())))
			{
				// The view toggle is presentation, not a rule: it goes straight to
				// the application callback instead of through the action map.
				if (action == InputAction::ToggleView)
				{
					if (m_OnViewToggle)
						m_OnViewToggle();
					continue;
				}

				// Same for the music mute.
				if (action == InputAction::ToggleMusic)
				{
					if (m_OnMusicToggle)
						m_OnMusicToggle();
					continue;
				}

				// And for skipping songs: the delta is the only thing the
				// application needs, so one callback carries it.
				if (action == InputAction::MusicNext)
				{
					if (m_OnMusicSkip)
						m_OnMusicSkip(+1);
					continue;
				}
				if (action == InputAction::MusicPrevious)
				{
					if (m_OnMusicSkip)
						m_OnMusicSkip(-1);
					continue;
				}

				const Action rule = RuleActionFor(action);
				if (rule == Action::None)
					continue;

				m_Manager->HandleAction(rule, true);

				// Refresh overlay on state-changing actions.
				if (m_Overlay)
				{
					if (rule == Action::TogglePause ||
						rule == Action::Restart)
					{
						m_Overlay->RefreshState();
					}
				}
			}
		}
		else if (KeyReleasedEvent* released = dynamic_cast<KeyReleasedEvent*>(&event))
		{
			for (const std::string& action : InputMap::GetActionsForKey(static_cast<KeyCode>(released->GetKeyCode())))
			{
				const Action rule = RuleActionFor(action);
				if (rule != Action::None)
					m_Manager->HandleAction(rule, false);
			}
		}
	}

}  // namespace Tetris
