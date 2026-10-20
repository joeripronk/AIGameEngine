#pragma once

#include "TetrisManager.h"
#include "TetrisOverlay.h"

#include "Forge/Core/Layer.h"

#include <functional>
#include <string>

namespace Tetris {

	// The game's input actions.
	//
	// The rules know an Action; the player knows a key. The binding between
	// the two is data - in the engine's input map - so the keys can be
	// changed without the rules changing, and the rules never learn what a
	// key code is.
	namespace InputAction {
		inline constexpr const char* MoveLeft = "move_left";
		inline constexpr const char* MoveRight = "move_right";
		inline constexpr const char* SoftDrop = "soft_drop";
		inline constexpr const char* HardDrop = "hard_drop";
		inline constexpr const char* RotateCW = "rotate_cw";
		inline constexpr const char* RotateCCW = "rotate_ccw";
		inline constexpr const char* TogglePause = "toggle_pause";
		inline constexpr const char* Restart = "restart";

		// Presentation, not rules: the view toggle, the music mute and the
		// music skip are the application's business.
		inline constexpr const char* ToggleView = "toggle_view";
		inline constexpr const char* ToggleMusic = "toggle_music";
		inline constexpr const char* MusicNext = "music_next";
		inline constexpr const char* MusicPrevious = "music_previous";

		// The light show is presentation too: the lamps are the game's
		// dressing, and the rules know nothing about light.
		inline constexpr const char* ToggleLightShow = "toggle_light_show";
	}

	// Puts the game's actions and their default bindings into the engine's
	// input map. An action that is already there keeps its bindings, so
	// this can be called again after a remap file has been loaded.
	//
	// Returns false when the map refused an action, which means the game
	// cannot be played - and a game that cannot be played says so rather
	// than starting and doing nothing.
	bool SetupInputMap();

	// The rule an input action stands for, or Action::None for an action
	// that is not one of the rules'.
	Action RuleActionFor(const std::string& inputAction);

	// A layer that turns input into game actions.
	//
	// Input arrives through the engine's event system rather than by polling GLFW
	// from the frame loop. Polling has to enumerate a key range to notice a change,
	// and an off-by-one in that range does not fail loudly - it just drops a key.
	// Events arrive for every key the window reports.
	//
	// The key is then looked up in the input map, so what a key means is
	// data and not a switch in the code.
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

		// The light show is the last piece of presentation: the lamps over the
		// playfield go on and off, and the rules know nothing about them.
		void SetLightShowToggledCallback(std::function<void()> callback) { m_OnLightShowToggle = std::move(callback); }

	private:
		TetrisManager* m_Manager = nullptr;
		OverlayLayer* m_Overlay = nullptr;
		std::function<void()> m_OnViewToggle;
		std::function<void()> m_OnMusicToggle;
		std::function<void(int)> m_OnMusicSkip;
		std::function<void()> m_OnLightShowToggle;
	};

}  // namespace Tetris
