#include "fgpch.h"

#include "TetrisInputLayer.h"

#include "Forge/Events/KeyEvent.h"

#include <GLFW/glfw3.h>

namespace Tetris {

Action MapKey(int keyCode)
{
	switch (keyCode)
	{
	case GLFW_KEY_LEFT:
	case GLFW_KEY_A: return Action::MoveLeft;

	case GLFW_KEY_RIGHT:
	case GLFW_KEY_D: return Action::MoveRight;

	case GLFW_KEY_DOWN:
	case GLFW_KEY_S: return Action::SoftDrop;

	case GLFW_KEY_SPACE: return Action::HardDrop;

	case GLFW_KEY_UP:
	case GLFW_KEY_W:
	case GLFW_KEY_X: return Action::RotateCW;

	case GLFW_KEY_Z: return Action::RotateCCW;

	case GLFW_KEY_P:
	case GLFW_KEY_ESCAPE: return Action::TogglePause;

	case GLFW_KEY_R: return Action::Restart;

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
			m_Manager->HandleAction(MapKey(pressed->GetKeyCode()), true);
	}
	else if (KeyReleasedEvent* released = dynamic_cast<KeyReleasedEvent*>(&event))
	{
		m_Manager->HandleAction(MapKey(released->GetKeyCode()), false);
	}
}

}  // namespace Tetris