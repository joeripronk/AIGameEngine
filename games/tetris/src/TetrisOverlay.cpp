#include "fgpch.h"

#include "TetrisOverlay.h"
#include "TetrisAnimation.h"

#include "Forge/UI/UI.h"

#include <vendor/imgui/imgui.h>

#include <cmath>
#include <sstream>
#include <cstdio>

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

	// Scoped to this window: ImGui records the style-colour stack depth at
	// Begin, and a push that is still live when End runs is the
	// "Missing PopStyleColor()" assert - in Debug the game aborts, in a
	// shipping build the colour leaks into every later window.
	{
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

		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 32.0f);
		ImGui::Text("  Music:");
		ImGui::SameLine();
		ImGui::TextColored({1.0f, 1.0f, 1.0f, 1.0f}, "M mute, N/B skip");
	}

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

	// Which song the music is on. The skip keys have no visible effect
	// otherwise, and a control whose effect cannot be seen is a control that
	// looks broken when it is working.
	if (m_Music && m_Music->TrackCount() > 0)
	{
		ImGui::TextColored({0.70f, 0.85f, 0.90f, 1.00f}, "MUSIC: %d/%d %s",
			m_Music->CurrentTrackIndex() + 1, m_Music->TrackCount(),
			m_Music->IsMuted() ? "muted" : m_Music->CurrentTitle());
	}

	// Fire meter: how close a fire block is. The bar fills as the chance
	// climbs; screen shake from hard drops and line clears charges it.
	{
		const float chance = m_Manager ? m_Manager->GetFireChance() : 0.0f;
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f);
		ImGui::TextColored({1.00f, 0.55f, 0.20f, 1.00f}, "FIRE");

		char overlay[16];
		std::snprintf(overlay, sizeof(overlay), "%3.0f%%", chance * 100.0f);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 1.00f, 0.42f, 0.08f, 1.00f });
		ImGui::ProgressBar(chance, ImVec2(windowWidth - 16.0f, 0.0f), overlay);
		ImGui::PopStyleColor();
	}

	// Active score multiplier and combo. The multiplier is the stacked tier
	// from recent clears (x2/x3/x4) with the turns left in its window; the
	// combo doubles per consecutive clear. Shown only when something is live,
	// so the panel does not advertise two x1 counters.
	if (m_Manager)
	{
		const auto& gstate = m_Manager->GetState();
		if (gstate.Multiplier > 1)
			ImGui::TextColored({1.00f, 0.85f, 0.30f, 1.00f}, "MULTIPLIER: x%d  (%d)",
				gstate.Multiplier, gstate.MultiplierTurns);
		if (gstate.ComboMultiplier > 1)
			ImGui::TextColored({1.00f, 0.55f, 0.85f, 1.00f}, "COMBO: x%d",
				gstate.ComboMultiplier);
	}

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

	// Reward toast: "Nice".."Epic" at the cleared rows, flashing and shaking,
	// the effect stronger the more lines were cleared. It fades out over a
	// line-count-dependent duration so a Tetris lingers longer than a single.
	if (m_RewardTimer > 0.0f && m_RewardLines > 0)
	{
		const int lines = m_RewardLines;
		const float duration = RewardDuration(lines);
		const float life = duration > 0.0f ? m_RewardTimer / duration : 0.0f;

		// Flash pulse plus a fade-out over the last third of its life.
		const float flash = RewardFlashPulse(m_RewardElapsed);
		const float fade = life < 0.34f ? (life / 0.34f) : 1.0f;
		const float alpha = (0.45f + 0.55f * flash) * fade;

		// Shake offset, amplitude scaled by lines so more lines shake harder.
		constexpr float kPi = 3.14159265358979323846f;
		const float amp = RewardShakeAmplitude(lines, 26.0f);
		const float phase = 2.0f * kPi * m_RewardElapsed;
		const float offX = amp * std::sin(4.3f * phase);
		const float offY = amp * std::sin(7.0f * phase);

		const float scale = RewardFontScale(lines);
		const char* label = TetrisManager::RewardLabelForLines(lines);

		char toast[32];
		std::snprintf(toast, sizeof(toast), "%s!", label);

		const ImVec2 center = { ImGui::GetIO().DisplaySize.x * 0.5f + offX,
			ImGui::GetIO().DisplaySize.y * 0.40f + offY };

		ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowBgAlpha(0.0f);
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
		ImGui::Begin("##RewardToast",
			nullptr,
			ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoSavedSettings);

		// Brighter and more saturated with more lines cleared.
		const float heat = 0.25f * static_cast<float>(lines > 4 ? 4 : lines);
		const ImVec4 color { 1.0f, 0.92f - 0.25f * heat, 0.45f - 0.35f * heat, alpha };

		ImGui::SetWindowFontScale(scale);
		ImGui::TextColored(color, "%s", toast);
		ImGui::SetWindowFontScale(1.0f);

		ImGui::End();
		ImGui::PopStyleColor();
	}
}

void OverlayLayer::OnUpdate(Forge::Timestep ts)
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

		// A new clear raises the reward toast; the timer counts it down so the
		// label fades out after a line-count-dependent duration.
		if (state.LastRewardLines != m_LastRewardLines)
		{
			m_LastRewardLines = state.LastRewardLines;
			if (m_LastRewardLines > 0)
			{
				m_RewardLines = m_LastRewardLines;
				m_RewardTimer = RewardDuration(m_RewardLines);
				m_RewardElapsed = 0.0f;
			}
		}
	}

	if (m_RewardTimer > 0.0f)
	{
		m_RewardTimer -= ts;
		m_RewardElapsed += ts;
		if (m_RewardTimer < 0.0f)
			m_RewardTimer = 0.0f;
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
