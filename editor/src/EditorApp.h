#pragma once

#include "Forge/Core/Application.h"
#include "Forge/Editor/EditorContext.h"
#include "Forge/Renderer/ViewportRenderer.h"

#include <string>

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
		// How long a notification stays on screen, in frames (~4s at 60fps).
		static constexpr int kNotificationFrames = 240;

		void CreateDefaultScene();
		void DrawMenuBar();
		void DrawHierarchyPanel();
		void DrawInspectorPanel();
		void DrawViewportPanel();
		void DrawAssetBrowserPanel();
		void DrawPropertiesPanel();
		void DrawGizmoToolbar();

		// Surfaces the result of a user-initiated action (save, export). Without
		// this the UI silently discards failures: a failed export would leave the
		// user believing they had a distributable game.
		void Notify(const std::string& message, bool isError);

		// Draws and then clears any pending notification.
		void DrawNotification();

		// Mirrors the currently playing scene's camera into the editor camera.
		void SyncEditorCamera();

		// Gizmo pointer interaction.
		void OnGizmoMouseDown(int button, int x, int y);
		void OnGizmoMouseMove(int x, int y);
		void OnGizmoMouseUp(int button, int x, int y);
		void DrawGizmoInteraction();

		EditorContext m_Editor;
		ViewportRenderer m_Viewport;

		std::string m_Notification;
		bool m_NotificationIsError = false;
		int m_NotificationFramesRemaining = 0;
	};

}