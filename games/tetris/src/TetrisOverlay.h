#pragma once

#include "TetrisManager.h"
#include "TetrisMusic.h"
#include "TetrisTypes.h"

#include "Forge/Core/Layer.h"

namespace Tetris {

// Overlay layer that renders control hints, game state and statistics
// on top of the scene via ImGui.
class OverlayLayer : public Forge::Layer
{
public:
	OverlayLayer() : Layer("TetrisOverlay") {}

	void OnImGuiRender() override;

	// Update cached values each frame so we always display current state.
	void OnUpdate(Forge::Timestep ts) override;

	// Refresh display when the game state changes (start, restart, game over).
	void RefreshState();

	// Set the manager instance to read live state from.
	void SetManager(TetrisManager* manager) { m_Manager = manager; }

	// Show which song the background music is on, so the skip keys have
	// something visible to act on. Null means the HUD says nothing about music.
	void SetMusic(const TetrisMusic* music) { m_Music = music; }

private:
	TetrisManager* m_Manager = nullptr;
	const TetrisMusic* m_Music = nullptr;

	// Cached state to avoid frequent manager queries during ImGui render.
	TetrisGameState::State m_LastState = TetrisGameState::State::Playing;
	int32_t m_LastScore = -1;
	int32_t m_LastLines = -1;
	int32_t m_LastLevel = -1;
};

}  // namespace Tetris
