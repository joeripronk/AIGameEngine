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

		// Add/remove component menus for the selected entity.
		void DrawComponentMenu(Entity& entity);

		// Hot-reload watch over texture assets (the Hazel AssetManager
		// pattern): a texture rewritten on disk is re-decoded by the renderer
		// cache on the next frame, and the change is reported here so the log
		// says why the viewport changed.
		void WatchAssetTextures();
		void PollAssetHotReloads(float dt);

		AssetWatcher m_AssetWatcher;
		float m_AssetPollAccumulator = 0.0f;

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

		VkFormat m_ImGuiRenderFormat = VK_FORMAT_UNDEFINED;

		VkSampler m_ViewportSampler = VK_NULL_HANDLE;

		std::string m_Notification;
		bool m_NotificationIsError = false;
		int m_NotificationFramesRemaining = 0;
	};

}