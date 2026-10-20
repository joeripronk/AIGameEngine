#include "fgpch.h"
#include "TetrisGameScript.h"

#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"

namespace Tetris {

	const char* TetrisGameScript::kClassName = "TetrisGame";

	void TetrisGameScript::OnCreate()
	{
		// The scene is the one the entity is in: the board's entities are
		// created there and nowhere else.
		if (!GetEntity())
			return;

		if (!m_BoardView.Initialize(GetEntity().GetScene()))
			FG_ERROR("Tetris: the game script could not attach the board to the scene");
	}

	void TetrisGameScript::OnUpdate(Forge::Timestep ts)
	{
		// The rules advance here, and the board view mirrors them into the
		// scene. The window reads the state; it does not drive it.
		m_BoardView.Update(ts.GetSeconds());
	}

	void TetrisGameScript::OnDestroy()
	{
		m_BoardView.Shutdown();
	}

}
