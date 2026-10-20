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
		EditorApp(const ApplicationCommandLineArgs& args);
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

		// Points an entity's mesh at a model and imports it.
		//
		// The path alone is not the geometry: the importer runs when the path
		// is set, so a mesh whose path was assigned after the component was
		// added has a path and no geometry - and is not drawn at all.
		void SetMeshModel(Entity entity, const std::string& path);

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
		//
		// Only while that camera is being *changed*: the scene camera is
		// followed when the author edits it, and left alone when the author
		// flies the editor camera - a camera that is overwritten every frame
		// is a camera that cannot be moved.
		void SyncEditorCamera();

		// Gizmo pointer interaction.
		bool OnGizmoMouseDown(int button, int x, int y);
		void OnGizmoMouseMove(int x, int y);
		void OnGizmoMouseUp(int button, int x, int y);

		// The pointer interaction on the viewport image: a click selects what
		// is under it, a drag moves the selection.
		//
		// It runs here, on the image itself, because the ray is built from
		// the image's rectangle: an overlay somewhere else in the window picks
		// against a rectangle that is not the picture, and every pick lands
		// somewhere else.
		void HandleViewportInteraction();

		// The world-space ray through a viewport pixel (editor camera's
		// view-projection), shared by gizmo dragging and click-to-select.
		void ViewportRay(int x, int y, glm::vec3& origin, glm::vec3& direction);

		// Where a world point lands in the viewport, in the image's own
		// pixels. False when the point is behind the camera: there is no
		// place on the picture for it.
		bool ProjectToViewport(const glm::vec3& world, ImVec2& outLocal);

		// Whether a press at that pixel is close enough to the selection to
		// be a grab of the gizmo.
		bool IsNearSelectedEntity(float x, float y);

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

		// The viewport image's size, in the image's own pixels. The ray a
		// click builds is unprojected through this rectangle, so it has to be
		// the picture's size and not the panel's.
		ImVec2 m_ViewportImageSize = ImVec2(0.0f, 0.0f);

		// Whether the pointer is over the 3D view. The camera flies while it
		// is: the keys belong to the viewport, the way they do in every other
		// editor, and a key that moves the camera while the author is typing
		// a name into the inspector is a key that has been misfiled.
		bool m_PointerOverViewport = false;

		// The scene camera's transform as it was last seen. The editor camera
		// follows the scene camera while that transform changes, and is left
		// alone otherwise - otherwise flying the camera is undone every frame.
		glm::vec3 m_SceneCameraPosition = glm::vec3(0.0f);
		glm::vec3 m_SceneCameraRotation = glm::vec3(0.0f);
		bool m_HasSceneCamera = false;

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
