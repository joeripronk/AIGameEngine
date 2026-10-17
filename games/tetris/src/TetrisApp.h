#pragma once

#include "TetrisBoardView.h"
#include "TetrisInputOverlayLayer.h"

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
	void SyncPreview();

	// Mode-specific decoration: the well walls and preview tray (3D) or the
	// flat border (2D), plus the next-piece preview root. Tracked so
	// ToggleViewMode can remove exactly what the previous mode added.
	void CreateDecor();
	void DestroyDecor();

	// Point the camera and light at the playfield for the current mode.
	void ApplyViewLayout();

	// Switch between the flat 2D view and the angled 3D well. The rules are
	// untouched: score, board and the queued piece survive the switch.
	void ToggleViewMode();

	Forge::Ref<Forge::Scene> m_Scene;
	BoardView m_BoardView;

	// Camera and light kept for the toggle: the entities persist, only their
	// transforms and parameters change with the mode.
	Forge::Entity m_CameraEntity;
	Forge::Entity m_LightEntity;
	std::vector<Forge::Entity> m_DecorEntities;

	// Owns the pushed layer: the stack does not take ownership of what is
	// pushed onto it.
	std::unique_ptr<InputOverlayLayer> m_InputOverlayLayer;

	// Preview blocks, one per cell of the queued piece.
	std::map<uint64_t, Forge::Entity> m_PreviewBlocks;
	Forge::Entity m_PreviewRoot;

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