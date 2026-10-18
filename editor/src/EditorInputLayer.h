#pragma once

#include "Forge/Core/Layer.h"

namespace Forge {

	class EditorApp;

	// The editor's keyboard shortcuts, as a layer (the Hazel editor pattern:
	// EditorLayer owns them in its event handler, so they arrive as events
	// rather than being polled).
	//
	// The set is Hazel's: Ctrl+N new scene, Ctrl+S save, Ctrl+D duplicate,
	// Delete destroy, W/E/R gizmo mode, F5 play toggle; F9 simulate toggle
	// (Unity's convention - the public editor drives simulate from a toolbar
	// button only).
	class EditorInputLayer : public Layer
	{
	public:
		EditorInputLayer(EditorApp& app)
			: Layer("EditorInput"), m_App(app) {}

		void OnEvent(Event& event) override;

	private:
		bool OnKeyPressed(class KeyPressedEvent& e);
		bool OnWindowDrop(class WindowDropEvent& e);

		EditorApp& m_App;
	};

}
