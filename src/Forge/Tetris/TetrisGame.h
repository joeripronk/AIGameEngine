#pragma once

#include "Forge/Core/Application.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/TetrisTypes.h"
#include "Forge/Editor/EditorContext.h"
#include <map>

#include <memory>

namespace Forge {

// Tetris game mode that manages the Tetris game logic.
class TetrisGame : public Layer
{
public:
	TetrisGame();
	~TetrisGame() override = default;

	void OnAttach() override;
	void OnDetach() override;
	void OnUpdate(float ts) override;

	void OnEvent(Event& event) override;

	// Game control.
	void StartGame();
	void StopGame();
	void PauseGame();
	void ResumeGame();

	// Piece control.
	void MovePiece(TetrisDirection direction);
	void RotatePiece();
	void HardDrop();

	// Query game state.
	const TetrisGameState& GetState() const { return m_State; }
	TetrisGameState& GetState() { return m_State; }
	bool IsRunning() const { return m_IsRunning; }
	bool IsPaused() const { return m_IsPaused; }

private:
	// Initialize the Tetris scene.
	void InitializeScene();

	// Create game board entity.
	Entity CreateBoardEntity();

	// Create piece preview entity.
	Entity CreatePiecePreviewEntity();

	// Create display entity.
	Entity CreateDisplayEntity();

	// Update game board visual.
	void UpdateBoardVisual();

	// Update piece preview visual.
	void UpdatePiecePreviewVisual();

	// Update display visual.
	void UpdateDisplayVisual();

	// Handle input.
	void HandleInput(int keyCode, bool pressed);

	TetrisGameState m_State;
	bool m_IsRunning = false;
	bool m_IsPaused = false;
	bool m_IsGameOver = false;

	// Scene entities.
	Entity m_BoardEntity;
	Entity m_DisplayEntity;
	Entity m_PreviewEntity;

	// Input state.
	std::map<int, bool> m_KeyDown;

	// Lua script.
	std::string m_LuaScript = "resources/scripts/tetris.lua";

	// Editor context.
	EditorContext m_Editor;
};

}
