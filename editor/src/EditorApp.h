#pragma once

#include "Forge/Core/Application.h"
#include "Forge/Editor/EditorContext.h"
#include "Forge/Renderer/ViewportRenderer.h"
#include "Forge/Assets/AssetWatcher.h"

#include <string>

#include <vulkan/vulkan.h>
#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

namespace Forge {

	class EditorApp : public Application
	{
	public:
		EditorApp(const class ApplicationCommandLineArgs& args);
		~EditorApp() override;

		void Run() override;

		// The editor model (window-independent; shared with AI tooling/tests).
		EditorContext& GetEditor() { return m_Editor; }

		// Surfaces the result of a user-initiated action (save, export). Without
		// this the UI silently discards failures: a failed export would leave the
		// user believing they had a distributable game.
		void Notify(const std::string& message, bool isError);

		// Saved to the scene file the work is in; Ctrl+S and the menu share the
		// one path so the shortcut cannot save somewhere the menu never looks.
		void SaveCurrentScene();

		// Public for the input layer, which runs the Ctrl+N shortcut.
		void CreateDefaultScene();

	private:
		// How long a notification stays on screen, in frames (~4s at 60fps).
		static constexpr int kNotificationFrames = 240;

		void DrawMenuBar();
		void DrawHierarchyPanel();
		void DrawInspectorPanel();
		void DrawViewportPanel();
		void DrawAssetBrowserPanel();
		void DrawPropertiesPanel();
		void DrawGizmoToolbar();

		// Fullscreen docking host (the Hazel editor's DockSpace): panels
		// rearrange by dragging, and the layout lives in the ImGui .ini so
		// the editor reopens the way the author left it. The initial layout
		// is built once, only when no saved layout exists.
		void DrawDockspace();

		// Live profiling view (the Hazel profiler overlay panel): per-scope
		// sample counts and timings from the instrumentor's aggregation,
		// refreshed every frame.
		void DrawProfilerPanel();

		// Add/remove component menus for the selected entity.
		void DrawComponentMenu(Entity& entity);

		// Hot-reload watch over texture assets (the Hazel AssetManager
		// pattern): a texture rewritten on disk is re-decoded by the renderer
		// cache on the next frame, and the change is reported here so the log
		// says why the viewport changed.
		void WatchWatchedAssets();
		void PollAssetHotReloads(float dt);

		AssetWatcher m_AssetWatcher;
		float m_AssetPollAccumulator = 0.0f;

		// Surfaces the result of a user-initiated action (save, export).
		// Draws and then clears any pending notification.
		void DrawNotification();

		// The bottom status strip (the Hazel status bar): what is open, what is
		// playing, and what the last frame drew, in one place.
		void DrawStatusBar();

		// Mirrors the currently playing scene's camera into the editor camera.
		void SyncEditorCamera();

		// Gizmo pointer interaction.
		void OnGizmoMouseDown(int button, int x, int y);
		void OnGizmoMouseMove(int x, int y);
		void OnGizmoMouseUp(int button, int x, int y);
		void DrawGizmoInteraction();

		// The world-space ray through a viewport pixel (editor camera's
		// view-projection), shared by gizmo dragging and click-to-select.
		void ViewportRay(int x, int y, glm::vec3& origin, glm::vec3& direction);

		// Click-to-select in the viewport (the Hazel SceneTree pattern).
		void PickAtScreenPoint(int x, int y);

		EditorContext m_Editor;
		ViewportRenderer m_Viewport;

		// ImGui Vulkan backend initialisation (dynamic rendering, internal descriptor pool).
		ImGui_ImplVulkan_InitInfo m_ImGuiVulkanInitInfo = {};

		// Edit buffers for ImGui text fields. These have to outlive the frame:
		// ImGui::InputText writes through this pointer every frame, so a local
		// would be re-seeded from the component each frame and discard anything
		// the author had typed.
		char m_AudioPathBuffer[256] = {};

		// Descriptor set for the viewport render texture (registered once with
		// ImGui_ImplVulkan_AddTexture after the texture is created).
		ImTextureID m_ViewportTextureId = 0;
		VkImageView m_ViewportTextureView = VK_NULL_HANDLE;  // the view m_ViewportTextureId samples

		VkFormat m_ImGuiRenderFormat = VK_FORMAT_UNDEFINED;

		VkSampler m_ViewportSampler = VK_NULL_HANDLE;

		std::string m_Notification;
		bool m_NotificationIsError = false;
		int m_NotificationFramesRemaining = 0;

		// The scene file the current work is saved to. The editor opens scenes
		// from the asset browser and the project, and Ctrl+S has to write back
		// where the work came from, not where the last session left it.
		std::string m_CurrentScenePath = "resources/scenes/editor.forge";

		// The DockBuilder initial layout runs exactly once per session, and
		// only when the .ini has no saved layout to honor.
		bool m_DockLayoutBuilt = false;

		// ImGui lifecycle, mirrored so teardown and the frame loop match what
		// was actually created: a failed backend init leaves the editor running
		// without its UI, and shutting down a backend that was never initialised
		// is what asserts.
		bool m_ImGuiContextLive = false;
		bool m_GlfwBackendLive = false;
		bool m_ImGuiReady = false;
	};

}
