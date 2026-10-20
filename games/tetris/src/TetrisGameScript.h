#pragma once

#include "TetrisBoardView.h"

#include "Forge/Scripting/ScriptableEntity.h"

namespace Tetris {

	// The game's rules, as a script the scene names.
	//
	// The scene says "this entity runs the Tetris game", and the game's
	// binary is what that name means: a native script is C++ in the binary,
	// so the name is resolved against the registry the game registered its
	// type in.
	//
	// The editor is a different binary and does not have the game, so the
	// entity is inert there - the same rule the engine follows for every
	// other native script, and the reason the game's rules are not the
	// engine's business.
	class TetrisGameScript : public Forge::ScriptableEntity
	{
	public:
		// The name the scene gives this script.
		static const char* kClassName;

		void OnCreate() override;
		void OnUpdate(Forge::Timestep ts) override;
		void OnDestroy() override;

		// The rules and the view of them. The window, the overlay and the
		// input layer read the state through here; none of them owns it.
		BoardView& GetBoardView() { return m_BoardView; }
		const BoardView& GetBoardView() const { return m_BoardView; }

	private:
		BoardView m_BoardView;
	};

}
