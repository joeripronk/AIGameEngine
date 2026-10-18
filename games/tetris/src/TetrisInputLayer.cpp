#include "fgpch.h"

#include "TetrisInputLayer.h"

#include "Forge/Events/KeyEvent.h"
#include "Forge/Core/KeyCodes.h"

namespace Tetris {

Action MapKey(int keyCode)
{
	using namespace Forge;

	switch (keyCode)
	{
	case Key::Left:
	case Key::A: return Action::MoveLeft;

	case Key::Right:
	case Key::D: return Action::MoveRight;

	case Key::Down:
	case Key::S: return Action::SoftDrop;

	case Key::Space: return Action::HardDrop;

	case Key::Up:
	case Key::W:
	case Key::X: return Action::RotateCW;

	case Key::Z: return Action::RotateCCW;

	case Key::P:
	case Key::Escape: return Action::TogglePause;

	case Key::R: return Action::Restart;

	default: return Action::None;
	}
}

void InputLayer::OnEvent(Forge::Event& event)
{
	using namespace Forge;

	if (KeyPressedEvent* pressed = dynamic_cast<KeyPressedEvent*>(&event))
	{
		// A repeat is the window's own autorepeat, not a new press. Acting on it
		// would move twice per tick on top of the manager's delayed repeat, which
		// is exactly the double-step DAS exists to avoid.
		if (pressed->GetRepeatCount() == 0)
		{
			// The view toggle is presentation, not a rule: it goes straight to
			// the application callback instead of through the action map.
			if (pressed->GetKeyCode() == GLFW_KEY_V)
			{
				if (m_OnViewToggle)
					m_OnViewToggle();
			}

			// Same for the music mute.
			if (pressed->GetKeyCode() == GLFW_KEY_M)
			{
				if (m_OnMusicToggle)
					m_OnMusicToggle();
			}

			// And for skipping songs: the delta is the only thing the
			// application needs, so one callback carries it.
			if (pressed->GetKeyCode() == GLFW_KEY_N)
			{
				if (m_OnMusicSkip)
					m_OnMusicSkip(+1);
			}
			else if (pressed->GetKeyCode() == GLFW_KEY_B)
			{
				if (m_OnMusicSkip)
					m_OnMusicSkip(-1);
			}

			Action action = MapKey(pressed->GetKeyCode());
			m_Manager->HandleAction(action, true);

			// Refresh overlay on state-changing actions.
			if (m_Overlay)
			{
				if (action == Action::TogglePause ||
					action == Action::Restart)
				{
					m_Overlay->RefreshState();
				}
			}
		}
	}
	else if (KeyReleasedEvent* released = dynamic_cast<KeyReleasedEvent*>(&event))
	{
		m_Manager->HandleAction(MapKey(released->GetKeyCode()), false);
	}
}

}  // namespace Tetris