#pragma once

#include "TetrisBoardView.h"
#include "TetrisInputLayer.h"

#include "Forge/Core/Application.h"
#include "Forge/Scene/Scene.h"

#include <cstdint>
#include <map>
#include <memory>

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

	Forge::Ref<Forge::Scene> m_Scene;
	BoardView m_BoardView;

	// Owns the pushed layer: the stack does not take ownership of what is
	// pushed onto it.
	std::unique_ptr<InputLayer> m_InputLayer;

	// Preview blocks, one per cell of the queued piece.
	std::map<uint64_t, Forge::Entity> m_PreviewBlocks;
	Forge::Entity m_PreviewRoot;

	uint64_t m_FrameCount = 0;

		// Set once FG_CAPTURE_PATH has been written, so the capture happens on a
		// single frame instead of rewriting the file on every one.
		bool m_Captured = false;
};

}  // namespace Tetris