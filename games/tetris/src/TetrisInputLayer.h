#pragma once

#include "TetrisManager.h"

#include "Forge/Core/Layer.h"

namespace Tetris {

// Map a key code to a player action. Pure, so the bindings can be checked
// without a window: a mapping that silently did nothing would leave the player
// pressing a key that does nothing, which is exactly the kind of failure this
// makes assertable.
Action MapKey(int keyCode);

// A layer that turns key events into game actions.
//
// Input arrives through the engine's event system rather than by polling GLFW
// from the frame loop. Polling has to enumerate a key range to notice a change,
// and an off-by-one in that range does not fail loudly - it just drops a key.
// Events arrive for every key the window reports.
class InputLayer : public Forge::Layer
{
public:
	explicit InputLayer(TetrisManager& manager) : m_Manager(&manager) {}
	~InputLayer() override = default;

	void OnEvent(Forge::Event& event) override;

private:
	TetrisManager* m_Manager = nullptr;
};

}  // namespace Tetris