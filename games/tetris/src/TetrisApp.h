#pragma once

#include "TetrisBoardView.h"
#include "TetrisInputOverlayLayer.h"
#include "TetrisMusic.h"

#include "Forge/Core/Application.h"
#include "Forge/Scene/Scene.h"

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "Platform/Vulkan/ImGuiLayer.h"

namespace Tetris {

// The Tetris game: a window, a scene, and the frame loop that drives them.
//
// Everything the game is lives in this directory. Nothing here is known to the
// engine - ForgeTetris links the same ForgeApp the runtime and editor do and
// asks it for a window, a renderer and a scene, and supplies the game itself.
class TetrisApp : public Forge::Application
{
public:
	TetrisApp(const Forge::ApplicationCommandLineArgs& args);
	~TetrisApp() override;

	void Run() override;

private:
	// Build the scene: camera, light, playfield backdrop and next-piece
	// preview. Returns false if it could not be built.
	bool CreateScene();

	// Mirror the queued piece into the preview blocks beside the board.
	void SyncPreview(float dt);

	// Mode-specific decoration: the well walls and preview tray (3D) or the
	// flat border (2D), plus the next-piece preview root. Tracked so
	// ToggleViewMode can remove exactly what the previous mode added.
	void CreateDecor();
	void DestroyDecor();

	// Point the camera and light at the playfield for the current mode.
	void ApplyViewLayout();

	// Fit the view to the window. The projection takes the window's shape, so
	// the image is never stretched, and the camera stands far enough back that
	// the whole playfield - the board and the preview beside it - is in frame
	// whatever shape the window is.
	void FitViewToViewport();

	// The window size the view was last fitted to. A resize is a change here,
	// and the frame loop fits the view again when it sees one.
	uint32_t m_FittedWidth = 0;
	uint32_t m_FittedHeight = 0;

	// Shake the camera after a landing, by how fast the blocks were moving when
	// they stopped. The camera's base position is what ApplyViewLayout set; the
	// shake is an offset off it that decays, and settles back onto it.
	void ApplyImpactShake(float dt);

	// Switch between the flat 2D view and the angled 3D well. The rules are
	// untouched: score, board and the queued piece survive the switch.
	void ToggleViewMode();

	// Mute or unmute the theme. Presentation only - the game state is
	// untouched, which is why this is a callback from the input layer and not
	// an Action.
	void ToggleMusic();

	// Move to another song in the playlist (N is +1, B is -1). Presentation
	// for the same reason as the mute.
	void SkipMusic(int delta);

	Forge::Ref<Forge::Scene> m_Scene;
	BoardView m_BoardView;

	// The theme on the Music bus, driven by the game state each frame.
	TetrisMusic m_Music;

	// Camera and light kept for the toggle: the entities persist, only their
	// transforms and parameters change with the mode.
	Forge::Entity m_CameraEntity;
	Forge::Entity m_LightEntity;
	std::vector<Forge::Entity> m_DecorEntities;

	// The camera's home, and the damped oscillation that carries it away from
	// home after a landing. The shake is an offset, never a drift: it decays
	// back onto the base position so the playfield is never left off-centre.
	glm::vec3 m_CameraBasePosition = glm::vec3(0.0f);
	float m_ShakeAmplitude = 0.0f;
	float m_ShakePhase = 0.0f;
	bool m_Shaking = false;

	// Owns the pushed layer: the stack does not take ownership of what is
	// pushed onto it.
	std::unique_ptr<InputOverlayLayer> m_InputOverlayLayer;

	// Preview blocks, one per cell of the queued piece. A queued fire block
	// flashes on this clock, the same pulse the falling fire block glows at.
	std::map<uint64_t, Forge::Entity> m_PreviewBlocks;
	Forge::Entity m_PreviewRoot;
	float m_PreviewFlash = 0.0f;

	uint64_t m_FrameCount = 0;

	// Set once FG_CAPTURE_PATH has been written, so the capture happens on a
	// single frame instead of rewriting the file on every one.
	bool m_Captured = false;

	// Visual mode: true is the angled 3D well, false is the flat 2D view.
	// 2D is the default; --3d on the command line and the V key switch it.
	bool m_ThreeDimensional = false;
	bool ParseCommandLineMode(const Forge::ApplicationCommandLineArgs& args);

	// ImGui lifecycle, delegated to the engine: the ImGuiLayer owns the
	// context and the GLFW/Vulkan backends, and stays unready if the backend
	// init fails - the game then runs without on-screen controls rather than
	// dying with them.
	bool InitImGui();
	void ShutdownImGui();

	std::unique_ptr<Forge::ImGuiLayer> m_ImGuiLayer;
};

}  // namespace Tetris