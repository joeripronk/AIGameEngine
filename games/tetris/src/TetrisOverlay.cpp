#include "fgpch.h"

#include "TetrisOverlay.h"

#include "Forge/UI/UI.h"

#include <vendor/imgui/imgui.h>

#include <sstream>

namespace Tetris {

void OverlayLayer::OnImGuiRender()
{
	const float displaySizeY = ImGui::GetIO().DisplaySize.y;
	auto& style = ImGui::GetStyle();
	style.WindowPadding = { 8.0f, 10.0f };

	// Top-left corner: controls
	ImGui::SetNextWindowPos({14.0f, 14.0f}, ImGuiCond_Always);
	ImGui::Begin("##ControlsOverlay",
		nullptr,
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings);

	ImGui::TextColored({0.95f, 0.95f, 1.0f, 1.0f}, "CONTROLS");
	ImGui::Separator();

	ImGui::SetCursorPosX(12.0f);
	Forge::UI::ScopedStyleColor helpColor(ImGuiCol_Text, { 0.75f, 0.80f, 0.85f, 0.90f });

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8.0f);
	ImGui::Text("  Move:");
	ImGui::SameLine();
	ImGui::TextColored({1.0f, 1.0f, 1.0f, 1.0f}, "Arrow keys or A/D");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 32.0f);
	ImGui::Text("  Rotate:");
	ImGui::SameLine();
	ImGui::TextColored({1.0f, 1.0f, 1.0f, 1.0f}, "Up/W/X or Z");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 32.0f);
	ImGui::Text("  Soft drop:");
	ImGui::SameLine();
	ImGui::TextColored({1.0f, 1.0f, 1.0f, 1.0f}, "Down/S");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 32.0f);
	ImGui::Text("  Hard drop:");
	ImGui::SameLine();
	ImGui::TextColored({1.0f, 1.0f, 1.0f, 1.0f}, "Space");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 32.0f);
	ImGui::Text("  Pause:");
	ImGui::SameLine();
	ImGui::TextColored({1.0f, 1.0f, 1.0f, 1.0f}, "P or Escape");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 32.0f);
	ImGui::Text("  Restart:");
	ImGui::SameLine();
	ImGui::TextColored({1.0f, 1.0f, 1.0f, 1.0f}, "R");

	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 32.0f);
	ImGui::Text("  View:");
	ImGui::SameLine();
	ImGui::TextColored({1.0f, 1.0f, 1.0f, 1.0f}, "V (2D/3D)");

	ImGui::End();

	// Top-right corner: game status
	const float windowWidth = 180.0f;
	ImGui::SetNextWindowPos(
		{ImGui::GetIO().DisplaySize.x - windowWidth - 14.0f, 14.0f},
		ImGuiCond_Always);
	ImGui::Begin("##StatusOverlay",
		nullptr,
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings);

	ImGui::TextColored({0.85f, 0.90f, 1.00f, 0.95f}, "STATS");
	ImGui::Separator();
	ImGui::TextColored({1.0f, 1.0f, 1.0f, 1.0f}, "LEVEL: %3d", m_LastLevel);
	ImGui::TextColored({0.80f, 0.90f, 1.00f, 1.00f}, "SCORE: %6d", m_LastScore);
	ImGui::TextColored({0.75f, 0.85f, 0.95f, 1.00f}, "LINES: %4d", m_LastLines);
	ImGui::TextColored({0.65f, 0.85f, 1.00f, 1.00f}, "STATE: %s",
		m_LastState == TetrisGameState::State::Menu ? "MENU" :
		m_LastState == TetrisGameState::State::Playing ? "PLAYING" :
		m_LastState == TetrisGameState::State::Paused ? "PAUSED" :
		m_LastState == TetrisGameState::State::GameOver ? "GAME OVER" : "UNKNOWN");

	ImGui::End();

	// Bottom-left: game state or instructions
	ImGui::SetNextWindowPos({14.0f, displaySizeY - 140.0f}, ImGuiCond_Always);
	ImGui::Begin("##StateOverlay",
		nullptr,
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_NoInputs |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings);

	if (m_LastState == TetrisGameState::State::Menu)
	{
		ImGui::TextColored({0.95f, 0.95f, 1.0f, 1.0f}, "EASY MODE");
		ImGui::Separator();
		ImGui::TextColored({0.80f, 0.90f, 1.00f, 1.00f}, "Press R to start");
	}
	else if (m_LastState == TetrisGameState::State::GameOver)
	{
		ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "GAME OVER");
		ImGui::Separator();
		ImGui::TextColored({1.0f, 1.0f, 1.0f, 1.0f}, "Final score: %d", m_LastScore);
		ImGui::TextColored({0.80f, 0.90f, 1.00f, 1.00f}, "Press R to restart");
	}
	else
	{
		ImGui::TextColored({0.95f, 0.95f, 1.0f, 1.0f}, "EASY MODE");
		ImGui::Separator();
		ImGui::TextColored({0.70f, 0.75f, 0.80f, 0.90f}, "Easy mode = 0 combo bonus");

		if (m_LastState == TetrisGameState::State::Paused)
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 10.0f);
			ImGui::TextColored({1.0f, 0.95f, 0.4f, 1.0f}, "PAUSED");
		}
	}

	ImGui::End();
}

void OverlayLayer::OnUpdate(Forge::Timestep /*ts*/)
{
	// Update cached state from the manager each frame.
	if (m_Manager)
	{
		const auto& state = m_Manager->GetState();
		if (m_LastState != state.CurrentState ||
			m_LastScore != state.Score ||
			m_LastLines != state.LinesCleared ||
			m_LastLevel != state.Level)
		{
			m_LastState = state.CurrentState;
			m_LastScore = state.Score;
			m_LastLines = state.LinesCleared;
			m_LastLevel = state.Level;
		}
	}
}

void OverlayLayer::RefreshState()
{
	// Force refresh on next ImGui render.
	if (m_Manager)
	{
		const auto& state = m_Manager->GetState();
		m_LastState = state.CurrentState;
		m_LastScore = state.Score;
		m_LastLines = state.LinesCleared;
		m_LastLevel = state.Level;
	}
}

}  // namespace Tetris
