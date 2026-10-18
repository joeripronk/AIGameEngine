#pragma once

#include "TetrisManager.h"
#include "TetrisOverlay.h"

#include "Forge/Core/Layer.h"

#include <functional>

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

	// Link the overlay so the input layer can refresh it on state changes.
	void SetOverlay(OverlayLayer* overlay) { m_Overlay = overlay; }

	// The view toggle is the app's, not the rules': switching 2D/3D changes
	// the presentation, not the game state, so it is a callback into the
	// application and never an Action.
	void SetViewToggledCallback(std::function<void()> callback) { m_OnViewToggle = std::move(callback); }

	// Muting the music is the same case as the view toggle: presentation, not
	// a rule. The rules do not know audio exists, so this cannot be an Action.
	void SetMusicToggledCallback(std::function<void()> callback) { m_OnMusicToggle = std::move(callback); }

	// Skip to another song in the playlist: +1 (N) forward, -1 (B) back. Also
	// presentation, so also a callback rather than an Action.
	void SetMusicSkippedCallback(std::function<void(int)> callback) { m_OnMusicSkip = std::move(callback); }

private:
	TetrisManager* m_Manager = nullptr;
	OverlayLayer* m_Overlay = nullptr;
	std::function<void()> m_OnViewToggle;
	std::function<void()> m_OnMusicToggle;
	std::function<void(int)> m_OnMusicSkip;
};

}  // namespace Tetris