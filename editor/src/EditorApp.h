#pragma once

#include "Forge/Core/Application.h"
#include "Forge/Editor/EditorContext.h"
#include "Forge/Renderer/ViewportRenderer.h"

namespace Forge {

	class EditorApp : public Application
	{
	public:
		EditorApp();
		~EditorApp() override;

		void Run() override;

		// The editor model (window-independent; shared with AI tooling/tests).
		EditorContext& GetEditor() { return m_Editor; }

	private:
		void CreateDefaultScene();
		void DrawMenuBar();
		void DrawHierarchyPanel();
		void DrawInspectorPanel();
		void DrawViewportPanel();
		void DrawAssetBrowserPanel();
		void DrawPropertiesPanel();
		void DrawGizmoToolbar();

		// Mirrors the currently playing scene's camera into the editor camera.
		void SyncEditorCamera();

		// Gizmo pointer interaction.
		void OnGizmoMouseDown(int button, int x, int y);
		void OnGizmoMouseMove(int x, int y);
		void OnGizmoMouseUp(int button, int x, int y);
		void DrawGizmoInteraction();

		EditorContext m_Editor;
		ViewportRenderer m_Viewport;
	};

}