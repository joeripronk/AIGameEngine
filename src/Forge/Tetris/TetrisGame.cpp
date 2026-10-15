#include "TetrisGame.h"
#include "Forge/Events/KeyEvent.h"
#include "Forge/Events/MouseEvent.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

namespace Forge {

// Tetromino definitions.
const TetrominoDefinition TetrisGameState::kTetrominoDefinitions[] = {
	// I
	{ { {0, 0}, {1, 0}, {2, 0}, {3, 0}}, glm::vec3(0.0f, 1.0f, 1.0f) },
	// O
	{ { {0, 0}, {1, 0}, {0, 1}, {1, 1}}, glm::vec3(1.0f, 1.0f, 0.0f) },
	// T
	{ { {0, 0}, {1, 0}, {2, 0}, {1, 1}}, glm::vec3(1.0f, 0.0f, 1.0f) },
	// S
	{ { {1, 0}, {2, 0}, {0, 1}, {1, 1}}, glm::vec3(0.0f, 1.0f, 0.0f) },
	// Z
	{ { {0, 0}, {1, 0}, {1, 1}, {2, 1}}, glm::vec3(1.0f, 0.0f, 0.0f) },
	// J
	{ { {0, 0}, {0, 1}, {1, 1}, {2, 1}}, glm::vec3(0.0f, 0.0f, 1.0f) },
	// L
	{ { {2, 0}, {0, 1}, {1, 1}, {2, 1}}, glm::vec3(1.0f, 0.5f, 0.0f) },
};

TetrisGame::TetrisGame()
{
	FG_CORE_INFO("TetrisGame created");
}

void TetrisGame::OnAttach()
{
	FG_CORE_INFO("TetrisGame attached");
	InitializeScene();
	m_IsRunning = true;
	m_IsPaused = false;
	m_IsGameOver = false;
}

void TetrisGame::OnDetach()
{
	FG_CORE_INFO("TetrisGame detached");
	StopGame();
}

void TetrisGame::OnUpdate(float ts)
{
	if (!m_IsRunning || m_IsPaused)
		return;

	// Update game state.
	m_State.Update(ts);

	// Update visuals.
	UpdateBoardVisual();
	UpdatePiecePreviewVisual();
	UpdateDisplayVisual();
}

void TetrisGame::OnEvent(Event& event)
{
	if (!m_IsRunning)
		return;

	// Handle key events.
	if (auto* keyEvent = event.TryCast<KeyPressedEvent>())
	{
		HandleInput(keyEvent->GetKeyCode(), true);
	}
	else if (auto* keyEvent = event.TryCast<KeyReleasedEvent>())
	{
		HandleInput(keyEvent->GetKeyCode(), false);
	}
}

void TetrisGame::StartGame()
{
	m_IsRunning = true;
	m_IsPaused = false;
	m_IsGameOver = false;
	m_State.StartGame();
	FG_CORE_INFO("Tetris game started");
}

void TetrisGame::StopGame()
{
	m_IsRunning = false;
	m_IsPaused = false;
	m_IsGameOver = false;
	FG_CORE_INFO("Tetris game stopped");
}

void TetrisGame::PauseGame()
{
	m_IsPaused = true;
	FG_CORE_INFO("Tetris game paused");
}

void TetrisGame::ResumeGame()
{
	m_IsPaused = false;
	FG_CORE_INFO("Tetris game resumed");
}

void TetrisGame::MovePiece(TetrisDirection direction)
{
	m_State.MovePiece(direction);
}

void TetrisGame::RotatePiece()
{
	m_State.RotatePiece();
}

void TetrisGame::HardDrop()
{
	m_State.HardDrop();
}

void TetrisGame::InitializeScene()
{
	// Create game board.
	m_BoardEntity = CreateBoardEntity();

	// Create display.
	m_DisplayEntity = CreateDisplayEntity();

	// Create piece preview.
	m_PreviewEntity = CreatePiecePreviewEntity();
}

Entity TetrisGame::CreateBoardEntity()
{
	Entity entity = m_Editor.GetScene()->CreateEntity("GameBoard");

	auto& transform = entity.AddComponent<TransformComponent>();
	transform.Translation = glm::vec3(0.0f, -10.0f, 0.0f);
	transform.Scale = glm::vec3(10.0f, 20.0f, 1.0f);

	auto& boardComp = entity.AddComponent<TetrisBoardComponent>();
	boardComp.Width = 10;
	boardComp.Height = 20;

	auto& material = entity.AddComponent<PBRMaterialComponent>();
	material.Albedo = glm::vec3(0.1f, 0.1f, 0.15f);
	material.Metallic = 0.0f;
	material.Roughness = 0.8f;

	auto& mesh = entity.AddComponent<MeshComponent>();
	mesh.ModelPath = "resources/models/cube.glb";
	mesh.Scale = glm::vec3(10.0f, 20.0f, 1.0f);

	return entity;
}

Entity TetrisGame::CreatePiecePreviewEntity()
{
	Entity entity = m_Editor.GetScene()->CreateEntity("NextPiece");

	auto& transform = entity.AddComponent<TransformComponent>();
	transform.Translation = glm::vec3(6.0f, -5.0f, 0.0f);

	auto& pieceComp = entity.AddComponent<TetrisPieceComponent>();
	pieceComp.Type = 0;
	pieceComp.State = 0;
	pieceComp.GridPosition = glm::ivec2(0, 0);
	pieceComp.Color = glm::vec3(0.0f, 1.0f, 1.0f);

	auto& material = entity.AddComponent<PBRMaterialComponent>();
	material.Albedo = glm::vec3(0.0f, 1.0f, 1.0f);
	material.Metallic = 0.2f;
	material.Roughness = 0.3f;

	return entity;
}

Entity TetrisGame::CreateDisplayEntity()
{
	Entity entity = m_Editor.GetScene()->CreateEntity("GameDisplay");

	auto& transform = entity.AddComponent<TransformComponent>();
	transform.Translation = glm::vec3(6.0f, -10.0f, 0.0f);

	auto& display = entity.AddComponent<TetrisDisplayComponent>();
	display.Score = 0;
	display.Level = 1;
	display.LinesCleared = 0;
	display.NextPiece = 0;
	display.GameStateText = "Menu";

	auto& material = entity.AddComponent<PBRMaterialComponent>();
	material.Albedo = glm::vec3(0.2f, 0.2f, 0.3f);
	material.Metallic = 0.0f;
	material.Roughness = 0.5f;

	return entity;
}

void TetrisGame::UpdateBoardVisual()
{
	if (!m_BoardEntity)
		return;

	auto& boardComp = m_BoardEntity.GetComponent<TetrisBoardComponent>();
	boardComp.Board = m_State.Board;
	boardComp.LinesToClear = m_State.LinesToClear;
}

void TetrisGame::UpdatePiecePreviewVisual()
{
	if (!m_PreviewEntity)
		return;

	auto& pieceComp = m_PreviewEntity.GetComponent<TetrisPieceComponent>();
	pieceComp.Type = static_cast<uint8_t>(m_State.NextPiece.Type);
	pieceComp.Color = m_State.NextPiece.Color;
}

void TetrisGame::UpdateDisplayVisual()
{
	if (!m_DisplayEntity)
		return;

	auto& display = m_DisplayEntity.GetComponent<TetrisDisplayComponent>();
	display.Score = m_State.Score;
	display.Level = m_State.Level;
	display.LinesCleared = m_State.LinesCleared;
	display.Combo = m_State.Combo;
	display.NextPiece = static_cast<uint8_t>(m_State.NextPiece.Type);

	switch (m_State.CurrentState)
	{
	case TetrisGameState::State::Menu:
		display.GameStateText = "Menu";
		break;
	case TetrisGameState::State::Playing:
		display.GameStateText = "Playing";
		break;
	case TetrisGameState::State::Paused:
		display.GameStateText = "Paused";
		break;
	case TetrisGameState::State::GameOver:
		display.GameStateText = "Game Over";
		break;
	}
}

void TetrisGame::HandleInput(int keyCode, bool pressed)
{
	if (pressed)
	{
		switch (keyCode)
		{
		case GLFW_KEY_LEFT:
			if (!m_IsPaused) MovePiece(TetrisDirection::Left);
			break;
		case GLFW_KEY_RIGHT:
			if (!m_IsPaused) MovePiece(TetrisDirection::Right);
			break;
		case GLFW_KEY_DOWN:
			if (!m_IsPaused) MovePiece(TetrisDirection::Down);
			break;
		case GLFW_KEY_UP:
			if (!m_IsPaused) RotatePiece();
			break;
		case GLFW_KEY_SPACE:
			if (!m_IsPaused) HardDrop();
			break;
		case GLFW_KEY_P:
			if (m_IsPaused) ResumeGame();
			else PauseGame();
			break;
		case GLFW_KEY_R:
			if (m_IsGameOver) StartGame();
			break;
		case GLFW_KEY_ESCAPE:
			StopGame();
			break;
		}
	}
	else
	{
		m_KeyDown[keyCode] = false;
	}
}

} // namespace Forge
