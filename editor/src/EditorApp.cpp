#include "fgpch.h"
#include "Forge/Debug/Instrumentor.h"
#include "editor/src/EditorApp.h"
#include "editor/src/EditorInputLayer.h"
#include "editor/src/FileDialog.h"

#include "Forge/Core/Log.h"
#include "Forge/Core/Time.h"
#include "Forge/UI/UI.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Assets/AssetRegistry.h"
#include "Forge/Project/GameExporter.h"
#include "Forge/Project/Project.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scripting/ScriptEngine.h"
#include "Forge/Scripting/NativeScriptRegistry.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "Platform/Vulkan/ImGuiLayer.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cstdio>
#include <cstdlib>

namespace Forge {

	namespace {

		// Copies a std::string into a fixed char buffer, always null-terminating
		// and truncating on overflow. std::snprintf is the portable form; strncpy
		// is deprecated by the MSVC CRT and does not guarantee termination.
		void CopyToBuffer(char* buffer, size_t size, const std::string& value)
		{
			std::snprintf(buffer, size, "%s", value.c_str());
		}

		// Ray origin and direction in world space for gizmo interaction.
		glm::vec3 g_GizmoRayOrigin;
		glm::vec3 g_GizmoRayDirection;
		int g_GizmoButton = -1;
		int g_GizmoAxis = -1;  // 0=X, 1=Y, 2=Z
		glm::vec3 g_GizmoStartPos;
		glm::vec3 g_GizmoStartValue;

		// Where the left button went down in the viewport, in panel-local
		// pixels. A release near that point is a click (select); a release far
		// from it is the tail of a drag and must not select.
		int g_MouseDownX = 0;
		int g_MouseDownY = 0;

		// Whether the left button went down in the viewport, and whether the
		// pointer has since moved far enough to be a drag rather than a click.
		bool g_PressedInViewport = false;
		bool g_DragStarted = false;

		// How far the pointer may travel before a press becomes a drag, in
		// pixels. Without it every click on an entity is also a drag, and a
		// click that moves the thing it clicked is not a click.
		constexpr int kDragThreshold = 4;

		// How near the selection a press has to be to be a grab of the gizmo,
		// in pixels.
		constexpr int kGizmoGrabRadius = 28;
	}

	EditorApp::EditorApp(const ApplicationCommandLineArgs& args)
		: Application(ApplicationSpecification{ "Forge Editor", "", args })
	{
		// No window, no surface, no renderer - the base has logged why.
		if (!StartupSucceeded())
			return;

		Renderer::Init();

		CreateDefaultScene();

		// --project names the project to work in, which is what says where the
		// assets are and which scene was open last. Without it the editor is
		// just an empty scene over whatever `resources` happens to hold.
		if (const std::string project = Application::GetArgumentValue(args, "--project"); !project.empty())
		{
			if (!Project::Load(project))
			{
				FG_ERROR("Could not open project '{0}'", project);
			}
			else
			{
				FG_INFO("Opened project '{0}'", project);

				// Reopening the scene the work was left in is the entire
				// reason a project file records one.
				const std::string editorScene = Project::GetEditorScenePath().string();
				if (!editorScene.empty() && m_Editor.OpenScene(editorScene))
					m_CurrentScenePath = editorScene;
				else if (!editorScene.empty())
					FG_ERROR("Could not open project scene '{0}'", editorScene);
			}
		}

		ImGui::CreateContext();
		m_ImGuiContextLive = true;

		ImGuiIO& io = ImGui::GetIO();

		// The engine's shared overlay configuration: keyboard navigation *and*
		// docking. Setting the navigation flag alone is what killed the editor:
		// DrawDockspace runs the DockBuilder calls, and with docking disabled
		// ImGui::DockSpace() returns without creating the node, so
		// DockBuilderAddNode() dereferenced the node it did not get on the
		// first frame. The window appeared and the process died in the frame
		// that was supposed to draw the UI - an editor that shows no GUI.
		ImGuiLayer::ApplyConfigFlags(io);

		ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(GetWindow().GetNativeWindow()), true);
		m_GlfwBackendLive = true;

		// Initialise the ImGui Vulkan backend against the present render
		// pass: the overlay's draws are recorded into the frame's command
		// buffer while that pass is still open, so the overlay's pipeline
		// has to be the one the pass was created with.
		//
		// It is not a dynamic rendering pipeline: that kind may only be
		// used inside a vkCmdBeginRendering scope, and the overlay draws
		// inside the open render pass. Asking for one produced a pipeline
		// that could not even be created (the device has no
		// dynamicRendering feature enabled), which is why the editor's
		// panels did not draw.
		//
		// The internal descriptor pool is large enough for the font atlas + one
		// descriptor set per ImGui texture (viewport image).
		m_ImGuiVulkanInitInfo = {};
		m_ImGuiVulkanInitInfo.ApiVersion = VK_API_VERSION_1_3;
		m_ImGuiVulkanInitInfo.Instance = static_cast<VkInstance>(Renderer::GetVulkanInstanceHandle());
		m_ImGuiVulkanInitInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(Renderer::GetVulkanPhysicalDeviceHandle());
		m_ImGuiVulkanInitInfo.Device = static_cast<VkDevice>(Renderer::GetVulkanDeviceHandle());
		m_ImGuiVulkanInitInfo.QueueFamily = Renderer::GetVulkanGraphicsQueueIndex();
		m_ImGuiVulkanInitInfo.Queue = static_cast<VkQueue>(Renderer::GetVulkanGraphicsQueueHandle());
		m_ImGuiVulkanInitInfo.PipelineCache = VK_NULL_HANDLE;
		m_ImGuiVulkanInitInfo.DescriptorPool = VK_NULL_HANDLE;
		m_ImGuiVulkanInitInfo.DescriptorPoolSize = 256;
		m_ImGuiVulkanInitInfo.Allocator = nullptr;
		m_ImGuiVulkanInitInfo.MinImageCount = Renderer::GetSwapchainImageCount();
		m_ImGuiVulkanInitInfo.ImageCount = Renderer::GetSwapchainImageCount();
		m_ImGuiVulkanInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		m_ImGuiVulkanInitInfo.RenderPass = static_cast<VkRenderPass>(Renderer::GetPresentRenderPass());
		m_ImGuiVulkanInitInfo.UseDynamicRendering = false;
		m_ImGuiRenderFormat = static_cast<VkFormat>(Renderer::GetSwapchainVkFormat());

		// A checked call, not an assertion. IM_ASSERT() discards its expression
		// when NDEBUG is defined, so every build without asserts never called
		// ImGui_ImplVulkan_Init at all: the renderer backend stayed null and the
		// first ImGui_ImplVulkan_NewFrame() dereferenced it. The shipping
		// builds had no UI for the same reason the Debug build crashed - with
		// no log line to say so.
		if (!Renderer::HasSwapchain())
		{
			FG_CORE_ERROR("Editor: no swapchain; the editor UI is disabled");
		}
		else if (!ImGui_ImplVulkan_Init(&m_ImGuiVulkanInitInfo))
		{
			FG_CORE_ERROR("Editor: ImGui Vulkan backend init failed; the editor UI is disabled");
		}
		else
		{
			m_ImGuiReady = true;
		}

		if (!m_ImGuiReady)
		{
			// Roll the backends and the context back the same way OnDetach
			// does, so the editor runs without its UI rather than dying with it.
			ImGui_ImplGlfw_Shutdown();
			m_GlfwBackendLive = false;
			ImGui::DestroyContext();
			m_ImGuiContextLive = false;
			Notify("Editor UI is disabled - see Forge.log", true);
		}

		m_Editor.GetCamera().SetPosition({ 0.0f, 5.0f, 12.0f });
		// Twenty degrees down: the engine's convention is that a positive
		// pitch looks up, and a camera that looks at the horizon shows the
		// author the sky instead of the scene.
		m_Editor.GetCamera().SetRotation({ -20.0f, 0.0f, 0.0f });

		m_Viewport.Init(800, 600);

		// Create a linear sampler for the viewport render texture.
		VkSamplerCreateInfo samplerInfo = {};
		samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = 0.0f;
		VkDevice device = static_cast<VkDevice>(Renderer::GetVulkanDeviceHandle());
		if (device)
			vkCreateSampler(device, &samplerInfo, nullptr, &m_ViewportSampler);

		// Keyboard shortcuts arrive as events through a layer, the way Hazel's
		// editor handles them; the LayerStack owns the layer.
		PushLayer(new EditorInputLayer(*this));
	}

	EditorApp::~EditorApp()
	{
		// Startup never got a window, so none of this was ever initialized;
		// shutting down an uninitialized backend is what asserts.
		if (!StartupSucceeded())
			return;

		if (m_ImGuiReady)
			ImGui_ImplVulkan_Shutdown();
		if (m_GlfwBackendLive)
			ImGui_ImplGlfw_Shutdown();
		m_Viewport.Shutdown();

		if (m_ViewportSampler != VK_NULL_HANDLE)
		{
			VkDevice device = static_cast<VkDevice>(Renderer::GetVulkanDeviceHandle());
			if (device)
				vkDestroySampler(device, m_ViewportSampler, nullptr);
			m_ViewportSampler = VK_NULL_HANDLE;
		}

		if (m_ImGuiContextLive)
			ImGui::DestroyContext();
	}

	void EditorApp::CreateDefaultScene()
	{
		m_Editor.NewScene();

		auto camera = m_Editor.CreateEntity("MainCamera");
		camera.GetComponent<TransformComponent>().Translation = { 0.0f, 5.0f, 12.0f };
		// The scene camera looks down at the scene. Its rotation is a
		// transform, so its angles are radians - and a positive pitch is up,
		// so the camera looks down with a negative one.
		camera.GetComponent<TransformComponent>().Rotation = { glm::radians(-20.0f), 0.0f, 0.0f };
		camera.AddComponent<CameraComponent>().Primary = true;

		auto light = m_Editor.CreateEntity("DirectionalLight");
		auto& lightComponent = light.AddComponent<LightComponent>();
		lightComponent.Type = LightComponent::Type::Directional;
		lightComponent.Direction = { -0.5f, -1.0f, -0.3f };

		// The cube is a thing to look at, to click and to move: it has
		// geometry, so it is drawn, and it can be picked.
		//
		// A path on a mesh component is not geometry - the geometry is
		// imported from it - so the path is set and then imported. A mesh
		// with a path and no geometry is not drawn at all, which is how the
		// default scene used to be empty.
		auto cube = m_Editor.CreateEntity("Cube");
		auto& cubeTransform = cube.GetComponent<TransformComponent>();
		cubeTransform.Translation = { 0.0f, 0.5f, 0.0f };
		cube.AddComponent<PBRMaterialComponent>().Albedo = { 0.8f, 0.3f, 0.3f };
		SetMeshModel(cube, "resources/models/cube.glb");

		auto physics = m_Editor.CreateEntity("PhysicsBox");
		physics.GetComponent<TransformComponent>().Translation = { 0.0f, 5.0f, 0.0f };
		auto& rigidbody = physics.AddComponent<RigidbodyComponent>();
		rigidbody.Type = RigidbodyComponent::BodyType::Dynamic;
		physics.AddComponent<BoxColliderComponent>().Size = { 1.0f, 1.0f, 1.0f };
		SetMeshModel(physics, "resources/models/cube.glb");

		// Two movers, one in Lua and one in C++, so the play mode shows the
		// two side by side: the same behaviour, and the same scene naming it.
		auto scriptEntity = m_Editor.CreateEntity("ScriptEntity");
		scriptEntity.GetComponent<TransformComponent>().Translation = { 4.0f, 0.5f, 0.0f };
		scriptEntity.AddComponent<ScriptComponent>().ClassName = "Mover";
		SetMeshModel(scriptEntity, "resources/models/cube.glb");

		// A native script is named in the scene the way a Lua script is, and
		// the play mode runs it.
		auto nativeEntity = m_Editor.CreateEntity("NativeScriptEntity");
		nativeEntity.GetComponent<TransformComponent>().Translation = { -4.0f, 0.5f, 0.0f };
		nativeEntity.AddComponent<NativeScriptComponent>().ClassName = "Mover";
		SetMeshModel(nativeEntity, "resources/models/cube.glb");

		m_Editor.SelectEntity(cube);
	}

	void EditorApp::SetMeshModel(Entity entity, const std::string& path)
	{
		// The component is added here: a mesh is a component and a path, and
		// an entity with only a path is an entity with nothing.
		auto& mesh = entity.AddOrReplaceComponent<MeshComponent>();
		mesh.ModelPath = path;

		// The import is what turns the path into geometry, and it has to be
		// asked for: a path that is only assigned is a name the entity does
		// not answer to.
		if (!m_Editor.GetScene()->ImportMeshData(entity))
			FG_CORE_WARN("Editor: '{0}' could not import '{1}'", entity.GetName(), path);
	}

	void EditorApp::SyncEditorCamera()
	{
		// Simulate is the opposite of syncing: the scene camera runs free and the
		// author watches it from their own fixed viewpoint.
		if (m_Editor.IsSimulate())
			return;

		// Follow the primary camera - but only while it is being changed.
		//
		// The camera was mirrored into the editor camera every frame, which
		// meant that the author could never move it: the keys moved the
		// camera and the next frame put it back. So the scene camera is
		// followed when the author edits *it*, and the editor camera is left
		// alone when the author flies *it*.
		auto cameraEntity = m_Editor.GetScene()->GetPrimaryCameraEntity();
		if (!cameraEntity)
		{
			m_HasSceneCamera = false;
			return;
		}

		const auto& tc = cameraEntity.GetComponent<TransformComponent>();
		const bool changed = !m_HasSceneCamera
			|| tc.Translation != m_SceneCameraPosition
			|| tc.Rotation != m_SceneCameraRotation;

		if (changed)
		{
			m_Editor.GetCamera().SetPosition(tc.Translation);

			// The scene's rotation is in radians (it is a transform, and a
			// transform's angles are radians) and the editor camera's is in
			// degrees. Copying one into the other without converting made the
			// editor's view turn fifteen times too fast for every degree the
			// scene camera turned.
			m_Editor.GetCamera().SetRotation(glm::degrees(tc.Rotation));

			m_SceneCameraPosition = tc.Translation;
			m_SceneCameraRotation = tc.Rotation;
			m_HasSceneCamera = true;
		}
	}

	void EditorApp::Run()
	{
		// An empty root scans the active project's asset directory.
		AssetRegistry::Scan();
		WatchWatchedAssets();

		uint64_t frameCount = 0;
		float lastFrameTime = 0.0f;
		while (IsRunning())
		{
			FG_PROFILE_SCOPE("Frame");

			// Poll and dispatch first: the window-close event must be able
			// to end the loop *before* this frame acquires a swapchain
			// image. Acquired after the close, the acquire blocks on a
			// surface that is gone, and the quit event sits in the queue
			// behind it forever - the editor spins after the window closes.
			PollEvents();
			ExecuteMainThreadQueue();
			if (!IsRunning())
				break;

			// DIAGNOSTIC ONLY: FG_RESIZE_TO=<w>x<h> asks the window to
			// change shape a few frames in, so the resize path can be
			// exercised where there is no window manager to drag the window
			// with.
			if (frameCount == 30)
			{
				if (const char* resizeTo = std::getenv("FG_RESIZE_TO"))
				{
					unsigned int w = 0;
					unsigned int h = 0;
					if (std::sscanf(resizeTo, "%ux%u", &w, &h) == 2 && w > 0 && h > 0)
					{
						FG_INFO("Editor: asking the window for {0}x{1}", w, h);
						GetWindow().SetSize(w, h);
					}
				}
			}
			++frameCount;

			float time = (float)Time::GetTime();
			Timestep timestep = time - lastFrameTime;
			lastFrameTime = time;

			PollAssetHotReloads(timestep);

			if (!IsMinimized())
			{
				for (Layer* layer : GetLayerStack())
					layer->OnUpdate(timestep);

				// The camera flies while the pointer is on the 3D view. It is
				// driven here rather than by a layer: the editor camera is
				// not a layer, and a camera that nobody updates is a camera
				// that does not move.
				//
				// A field with the keyboard focus owns the keyboard: typing
				// a name into the inspector must not fly the camera.
				const bool cameraInput = m_PointerOverViewport && !ImGui::IsAnyItemActive();
				m_Editor.GetCamera().OnUpdate(timestep, cameraInput);

				SyncEditorCamera();
				m_Editor.Update(timestep);

				// Acquire a swapchain image for this frame.
				const uint32_t swapchainImage = Renderer::AcquireSwapchainImage();

				// Open the frame's command list and render the scene into the
				// swapchain image (G-buffer → shadow → SSAO → HDR → tonemapped
				// → final composite).
				Renderer::BeginScene();
				Renderer::RenderScene(m_Editor.GetScene().get(), swapchainImage);

				// The author's view replaces the scene camera's draw in the
				// HDR target *before* the post chain composites it; recorded
				// after BeginSSAOPass it would never reach the screen.
				m_Viewport.Render(m_Editor.GetScene().get(), m_Editor.GetViewProjection());

				Renderer::BeginSSAOPass();
				Renderer::BeginBloomPass();
				Renderer::BeginHDRPass();
				Renderer::BeginFinalPass();

				// Render ImGui into the command list BEFORE it is closed.
				// GetCurrentCommandBuffer() is only valid between BeginScene()
				// and EndScene(). When the backend init failed the whole block is
				// skipped: the editor keeps rendering the scene and keeps
				// answering its agent, which is the engine's contract for a
				// missing overlay - run without the UI, do not die with it.
				if (m_ImGuiReady)
				{
					ImGui_ImplGlfw_NewFrame();
					ImGui_ImplVulkan_NewFrame();
					ImGui::NewFrame();

					// The docking host first: every panel Begin below docks into
					// it. The menu bar, toolbar, status bar and notifications
					// are chrome and stay floating above the layout.
					DrawDockspace();

					DrawMenuBar();
					DrawGizmoToolbar();
					DrawHierarchyPanel();
					DrawInspectorPanel();
					DrawViewportPanel();
					DrawAssetBrowserPanel();
					DrawPropertiesPanel();
					DrawProfilerPanel();
					DrawStatusBar();

					// Drawn last so it sits above every other panel.
					DrawNotification();

					ImGui::Render();
					VkCommandBuffer cmdBuf = static_cast<VkCommandBuffer>(Renderer::GetCurrentCommandBuffer());
					if (cmdBuf != VK_NULL_HANDLE)
						ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
				}

				// EndFinalPass is a no-op (the final pass is the last render
				// pass; EndScene closes the command list).
				Renderer::EndScene();

				// Present the swapchain image.
				Renderer::PresentSwapchainImage(swapchainImage);
			}
		}
	}

	void EditorApp::Notify(const std::string& message, bool isError)
	{
		m_Notification = message;
		m_NotificationIsError = isError;

		// Stay visible for roughly four seconds. A message that vanishes in one
		// frame is easy to miss, and an error the user never saw is the same as
		// an error that never happened.
		m_NotificationFramesRemaining = kNotificationFrames;

		if (isError)
			FG_CORE_ERROR("Editor: {0}", message);
		else
			FG_CORE_INFO("Editor: {0}", message);
	}

	void EditorApp::SaveCurrentScene()
	{
		const bool saved = m_Editor.SaveScene(m_CurrentScenePath);
		Notify(saved ? "Scene saved to " + m_CurrentScenePath : "Failed to save scene", !saved);
	}

	void EditorApp::DrawNotification()
	{
		if (m_Notification.empty() || m_NotificationFramesRemaining <= 0)
			return;

		// Anchored to the top-centre so it reads as transient feedback rather
		// than a permanent panel.
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(
			ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + 24.0f),
			ImGuiCond_Always, ImVec2(0.5f, 0.0f));

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoDocking;

		ImGui::Begin("##Notification", nullptr, flags);

		// Scoped, and the scope ends before End(): ImGui records the
		// style-colour stack depth when a window begins, and End() asserts
		// that the stack is back where it started. A colour still pushed
		// when End() runs is the "Missing PopStyleColor()" assert - in
		// Debug the editor aborts, and in a shipping build the colour
		// leaks into every later window.
		{
			Forge::UI::ScopedStyleColor color(ImGuiCol_Text, m_NotificationIsError
				? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
				: ImVec4(0.55f, 0.9f, 0.6f, 1.0f));
			ImGui::TextUnformatted(m_Notification.c_str());
		}

		if (ImGui::Button("Dismiss"))
		{
			m_Notification.clear();
			m_NotificationFramesRemaining = 0;
		}

		ImGui::End();

		--m_NotificationFramesRemaining;
	}

	void EditorApp::DrawStatusBar()
	{
		// Anchored to the bottom of the main viewport as a thin strip that
		// never moves and never overlaps a panel - the status bar answers
		// "what is the editor doing right now" without being asked.
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		const float barHeight = ImGui::GetFrameHeight() * 1.1f;

		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - barHeight),
			ImGuiCond_Always);
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, barHeight), ImGuiCond_Always);

		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
			| ImGuiWindowFlags_NoMove
			| ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_NoSavedSettings
			| ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoNav
			| ImGuiWindowFlags_NoBringToFrontOnFocus
			| ImGuiWindowFlags_NoDocking;

		ImGui::Begin("##StatusBar", nullptr, flags);

		const RendererStatistics stats = Renderer::GetStatistics();
		ImGui::Text("%s", m_CurrentScenePath.c_str());
		ImGui::SameLine();
		ImGui::Text("|  %s", m_Editor.IsPlaying() ? (m_Editor.IsPaused() ? (m_Editor.IsSimulate() ? "Simulating (paused)" : "Playing (paused)") : (m_Editor.IsSimulate() ? "Simulating" : "Playing")) : "Stopped");
		ImGui::SameLine();
		ImGui::Text("|  Selected: %s", m_Editor.HasSelection() ? m_Editor.GetSelectedEntity().GetName().c_str() : "None");
		ImGui::SameLine();
		const glm::vec3 cameraPosition = m_Editor.GetCamera().GetPosition();
		ImGui::Text("|  Camera: (%.1f, %.1f, %.1f)", cameraPosition.x, cameraPosition.y, cameraPosition.z);
		ImGui::SameLine();
		ImGui::Text("|  Draw calls: %u  Culled: %u", stats.DrawCalls3D, stats.FrustumCulled);

		ImGui::End();
	}

	void EditorApp::DrawMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene"))
				CreateDefaultScene();

			// Dialogs: "..." asks the OS, a plain item keeps its keyboard
			// shortcut on the last-used path. The two spellings of "save"
			// are the two questions the author has: "save my work" (Ctrl+S,
			// silent, into the file it came from) and "put it somewhere
			// else" (a path picked in the dialog).
			if (ImGui::MenuItem("Open Scene..."))
			{
				const std::string path = FileDialog::OpenFile("Forge Scene", "forge");
				if (!path.empty())
				{
					const bool opened = m_Editor.OpenScene(path);
					Notify(opened ? "Opened " + path : "Failed to open " + path, !opened);
					if (opened)
						m_CurrentScenePath = path;
				}
			}

			if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
				SaveCurrentScene();

			if (ImGui::MenuItem("Save Scene As..."))
			{
				const std::string path = FileDialog::SaveFile("Forge Scene", "forge",
					std::filesystem::path(m_CurrentScenePath).filename().string());
				if (!path.empty())
				{
					const bool saved = m_Editor.SaveScene(path);
					Notify(saved ? "Saved " + path : "Failed to save " + path, !saved);
					if (saved)
						m_CurrentScenePath = path;
				}
			}

			ImGui::Separator();
			// A project records the start scene, so a distributed runtime knows
			// what to open. It is saved from the editor rather than authored by
			// hand, which keeps the two from drifting apart.
			if (ImGui::MenuItem("Save Project..."))
			{
				const std::string path = FileDialog::SaveFile("Forge Project", "forgeproject", "project.forgeproject");
				if (!path.empty())
				{
					const bool saved = m_Editor.SaveProject(path);
					Notify(saved ? "Project saved to " + path : "Failed to save project", !saved);
				}
			}

			if (ImGui::MenuItem("Open Project..."))
			{
				const std::string path = FileDialog::OpenFile("Forge Project", "forgeproject");
				if (!path.empty())
				{
					const bool opened = m_Editor.OpenProject(path);
					Notify(opened ? "Opened project " + path : "Failed to open project", !opened);
				}
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Export Game..."))
			{
				GameExporter::Options opts;
				opts.OutputDirectory = "dist/";
				opts.ScenePath = "resources/scenes/editor.forge";
				// The resource root is left to the project: repeating it here
				// is how an export ends up shipping the wrong tree.
				opts.RuntimeExecutable = "bin/ForgeRuntime";
				opts.GameName = "MyGame";

				const bool exported = GameExporter::Export(opts);
				Notify(exported ? "Game exported to dist/" : "Export failed - see Forge.log for details", !exported);
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Exit"))
				Close();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Edit"))
		{
			// The names say what will be taken back, so the menu answers
			// "what does this undo?" and not only "can I undo?".
			const std::string undoName = m_Editor.GetUndoName();
			const std::string redoName = m_Editor.GetRedoName();

			if (ImGui::MenuItem(undoName.empty() ? "Undo" : ("Undo " + undoName).c_str(), "Ctrl+Z", false, m_Editor.CanUndo()))
				(void)m_Editor.Undo();

			if (ImGui::MenuItem(redoName.empty() ? "Redo" : ("Redo " + redoName).c_str(), "Ctrl+Y", false, m_Editor.CanRedo()))
				(void)m_Editor.Redo();

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Scene"))
		{
			if (ImGui::MenuItem("Create Empty Entity"))
				m_Editor.SelectEntity(m_Editor.CreateEntity("Entity"));
			if (ImGui::MenuItem("Create Cube"))
			{
				auto cube = m_Editor.CreateEntity("Cube");
				cube.GetComponent<TransformComponent>().Translation = { 0.0f, 0.5f, 0.0f };
				cube.AddComponent<PBRMaterialComponent>();
				// The geometry comes with it: a cube with a material and no
				// mesh is not drawn, and an object that cannot be seen cannot
				// be clicked either.
				SetMeshModel(cube, "resources/models/cube.glb");
				m_Editor.SelectEntity(cube);
			}
			if (ImGui::MenuItem("Create Sphere"))
			{
				auto sphere = m_Editor.CreateEntity("Sphere");
				sphere.GetComponent<TransformComponent>().Translation = { 0.0f, 0.5f, 0.0f };
				sphere.AddComponent<PBRMaterialComponent>();
				SetMeshModel(sphere, "resources/models/sphere.glb");
				m_Editor.SelectEntity(sphere);
			}
			if (ImGui::MenuItem("Create Audio Source"))
			{
				// Added here because there is no generic "Add Component" menu: an
				// audio source was previously only reachable by hand-editing the
				// scene file or driving EditorContext from an agent.
				auto source = m_Editor.CreateEntity("AudioSource");
				source.AddComponent<AudioSourceComponent>();
				m_Editor.SelectEntity(source);
			}
			if (ImGui::MenuItem("Delete Selected"))
			{
				if (m_Editor.HasSelection())
					m_Editor.DestroyEntity(m_Editor.GetSelectedEntity());
			}

			ImGui::EndMenu();
		}

		ImGui::EndMainMenuBar();
	}

	// Fullscreen docking host (the DockSpace pattern from Hazel's public
	// editor layer): one invisible window covering the viewport that every
	// panel docks into. Panels rearrange by dragging them onto a highlighted
	// edge/tab, and ImGui persists the layout in imgui.ini, so the editor
	// reopens exactly the way the author left it.
	//
	// The host flags are the public editor layer's recipe: NoDocking (a
	// dockable DockSpace host is a confusion of two drop targets), the
	// fullscreen no-chrome flags, NoBackground for the passthru central
	// node, and zero window padding so panel contents line up with the
	// viewport pixel grid the picking maths already uses.
	void EditorApp::DrawDockspace()
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->WorkPos);
		ImGui::SetNextWindowSize(viewport->WorkSize);
		ImGui::SetNextWindowViewport(viewport->ID);

		ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
			| ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("##Dockspace", nullptr, hostFlags);
		ImGui::PopStyleVar(3);

		const ImGuiID dockspaceId = ImGui::GetID("ForgeDockSpace");

		// A saved layout is honoured; a .ini that describes none is not. The
		// question is the file's *contents*, not its existence: an .ini written
		// by a build that docked nothing - which is what any working copy of
		// this repo has lying next to the executable - restores every panel as
		// a floating window at its saved size, the Viewport among them at its
		// 32x35 minimum, all of them stacked at 60,60. The 3D scene then fills
		// the window and the panels are unreadable fragments in the corner,
		// which is exactly an editor whose GUI "does not show".
		if (!m_DockLayoutBuilt)
		{
			m_DockLayoutBuilt = true;

			const char* iniText = ImGui::SaveIniSettingsToMemory();
			if (ImGuiLayer::HasSavedDockLayout(iniText ? iniText : ""))
			{
				FG_CORE_INFO("Editor: honouring the saved dock layout");
			}
			else
			{
				// 20% hierarchy on the left; the rest splits into the viewport
				// centre, a 30%-deep asset shelf at the bottom, and a right
				// column (inspector above, properties + profiler tabbed below).
				ImGuiID left = 0, right = 0, centre = 0, bottom = 0, rightTop = 0, rightBottom = 0;
				ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
				ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

				ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.20f, &left, &right);
				ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.30f, &bottom, &centre);
				ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.26f, &rightTop, &centre);
				ImGui::DockBuilderSplitNode(rightTop, ImGuiDir_Down, 0.55f, &rightBottom, &rightTop);

				ImGui::DockBuilderDockWindow("Scene Hierarchy", left);
				ImGui::DockBuilderDockWindow("Viewport", centre);
				ImGui::DockBuilderDockWindow("Inspector", rightTop);
				ImGui::DockBuilderDockWindow("Properties", rightBottom);
				ImGui::DockBuilderDockWindow("Profiler", rightBottom);
				ImGui::DockBuilderDockWindow("Assets", bottom);
				ImGui::DockBuilderFinish(dockspaceId);

				// DockBuilderFinish does not mark the settings dirty, so the
				// layout only reaches disk on a clean exit. Marked here, it is
				// written within the session: a layout that survives a killed
				// process is a layout the author can actually keep.
				ImGui::MarkIniSettingsDirty();

				FG_CORE_INFO("Editor: built the initial dock layout ({0})",
					(iniText && iniText[0]) ? "no dock layout in the .ini" : "no .ini to honour");
			}
		}

		// A panel with a 370-pixel minimum cannot survive a half-width
		// split; the public editor layer lowers the minimum for the dock
		// layout and restores it after.
		ImGuiStyle& style = ImGui::GetStyle();
		const float minWinSizeX = style.WindowMinSize.x;
		style.WindowMinSize.x = 370.0f;

		ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

		style.WindowMinSize.x = minWinSizeX;

		ImGui::End();
	}

	void EditorApp::DrawGizmoToolbar()
	{
		// A chrome strip, not a panel: it floats above the docking layout
		// and refuses to be docked into it.
		ImGui::Begin("Gizmo", nullptr, ImGuiWindowFlags_NoDocking);
		ImGui::Text("Mode:");
		ImGui::SameLine();
		if (ImGui::RadioButton("Move (1)", m_Editor.GetGizmoMode() == Gizmo::Mode::Translate))
			m_Editor.SetGizmoMode(Gizmo::Mode::Translate);
		ImGui::SameLine();
		if (ImGui::RadioButton("Rotate (2)", m_Editor.GetGizmoMode() == Gizmo::Mode::Rotate))
			m_Editor.SetGizmoMode(Gizmo::Mode::Rotate);
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale (3)", m_Editor.GetGizmoMode() == Gizmo::Mode::Scale))
			m_Editor.SetGizmoMode(Gizmo::Mode::Scale);

		ImGui::Text("Snap:");
		ImGui::SameLine();
		float snap = m_Editor.GetTranslationSnap();
		if (ImGui::DragFloat("##snap", &snap, 0.05f, 0.0f, 10.0f))
			m_Editor.SetTranslationSnap(snap);
		ImGui::End();
	}

	void EditorApp::DrawHierarchyPanel()
	{
		ImGui::Begin("Scene Hierarchy");

		for (Entity entity : m_Editor.GetAllEntities())
		{
			ImGui::PushID((int)entity);
			bool selected = (entity == m_Editor.GetSelectedEntity());
			if (ImGui::Selectable(entity.GetName().c_str(), selected))
				m_Editor.SelectEntity(entity);
			ImGui::PopID();
		}

		ImGui::End();
	}

	void EditorApp::DrawInspectorPanel()
	{
		ImGui::Begin("Inspector");

		if (!m_Editor.HasSelection())
		{
			ImGui::TextDisabled("No entity selected");
			ImGui::End();
			return;
		}

		Entity entity = m_Editor.GetSelectedEntity();

		// The inspector edits the components in place, and there is no other
		// way to know what the entity looked like before. The editor
		// remembers the state, and the change becomes a command when the
		// panel is done.
		m_Editor.BeginEntityEdit(entity);

		char nameBuffer[256];
		CopyToBuffer(nameBuffer, sizeof(nameBuffer), entity.GetName());
		if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
			entity.GetComponent<TagComponent>().Tag = nameBuffer;

		ImGui::Separator();

		// Generic add/remove. Without this, a component is only obtainable
		// through a purpose-built "Create ..." item, so anything not covered by
		// one of those menus cannot be authored in the GUI at all.
		DrawComponentMenu(entity);

		ImGui::Separator();
		ImGui::Text("Transform");

		auto& tc = entity.GetComponent<TransformComponent>();
		ImGui::DragFloat3("Translation", &tc.Translation.x, 0.1f);
		ImGui::DragFloat3("Rotation", &tc.Rotation.x, 1.0f);
		ImGui::DragFloat3("Scale", &tc.Scale.x, 0.01f);

		ImGui::Separator();
		ImGui::Text("Components");

		if (entity.HasComponent<LabelComponent>())
		{
			auto& label = entity.GetComponent<LabelComponent>();
			ImGui::Text("Label");

			char textBuffer[256];
			CopyToBuffer(textBuffer, sizeof(textBuffer), label.Text);
			if (ImGui::InputText("Text##label", textBuffer, sizeof(textBuffer)))
				label.Text = textBuffer;

			char fontBuffer[256];
			CopyToBuffer(fontBuffer, sizeof(fontBuffer), label.FontPath);
			if (ImGui::InputText("Font##label", fontBuffer, sizeof(fontBuffer)))
				label.FontPath = fontBuffer;

			ImGui::DragFloat("Size##label", &label.Size, 0.01f, 0.01f, 100.0f);
			ImGui::Checkbox("Billboard##label", &label.Billboard);
		}

		if (entity.HasComponent<MeshComponent>())
		{
			auto& mesh = entity.GetComponent<MeshComponent>();
			ImGui::Text("Mesh");

			// The model is the one thing about a mesh an author has to
			// point at: without it the entity renders a unit cube, and
			// there is no other way to change that from the editor.
			char modelBuffer[256];
			CopyToBuffer(modelBuffer, sizeof(modelBuffer), mesh.ModelPath);
			if (ImGui::InputText("Model##mesh", modelBuffer, sizeof(modelBuffer)))
			{
				mesh.ModelPath = modelBuffer;

				// The geometry follows the path: a path that names a file is
				// imported as it is typed, so the author sees the model they
				// named. A path that names nothing yet - half typed - is left
				// alone, because the import reports a failure for every
				// keystroke otherwise.
				if (std::filesystem::exists(modelBuffer))
					(void)m_Editor.GetScene()->ImportMeshData(entity);
			}

			if (ImGui::Button("Browse##mesh"))
			{
				const std::string chosen = FileDialog::OpenFile("Forge Model", "glb,gltf,obj,fbx");
				if (!chosen.empty())
				{
					mesh.ModelPath = chosen;
					(void)m_Editor.GetScene()->ImportMeshData(entity);
				}
			}
		}

		if (entity.HasComponent<MultiMeshComponent>())
		{
			auto& multi = entity.GetComponent<MultiMeshComponent>();
			ImGui::Text("Multi Mesh");

			char modelBuffer[256];
			CopyToBuffer(modelBuffer, sizeof(modelBuffer), multi.ModelPath);
			if (ImGui::InputText("Model##multimesh", modelBuffer, sizeof(modelBuffer)))
				multi.ModelPath = modelBuffer;

			// The instances' places are the data, and they are a list of
			// matrices: the editor shows how many there are rather than
			// pretending it can edit them.
			ImGui::TextDisabled("%zu instances", multi.Transforms.size());
		}

		if (entity.HasComponent<CameraComponent>())
		{
			auto& camera = entity.GetComponent<CameraComponent>();

			ImGui::Text("Camera");
			if (ImGui::Checkbox("Primary Camera##camera", &camera.Primary) && camera.Primary)
			{
				// Marking one camera primary demotes the others: two primaries
				// leave "the primary camera" with no defined answer. The checkbox
				// can only be reached on an entity that has the component, so a
				// failure here would mean the scene changed under the inspector.
				if (!m_Editor.GetScene()->SetPrimaryCamera(entity))
					Notify("Could not make this the primary camera", true);
			}

			const char* projections[] = { "Perspective", "Orthographic" };
			int currentProjection = (int)camera.Type;
			if (ImGui::Combo("Projection##camera", &currentProjection, projections, IM_ARRAYSIZE(projections)))
			{
				camera.Type = (CameraComponent::ProjectionType)currentProjection;
				camera.UpdateProjection();
			}

			// Each projection has the parameters it uses; showing the other
			// one's invites editing a field that does nothing.
			if (camera.Type == CameraComponent::ProjectionType::Orthographic)
			{
				if (ImGui::DragFloat("Orthographic Size##camera", &camera.OrthographicSize, 0.1f))
					camera.UpdateProjection();
			}
			else if (ImGui::DragFloat("Field of View##camera", &camera.FieldOfView, 0.5f))
			{
				camera.UpdateProjection();
			}

			ImGui::DragFloat("Near Clip##camera", &camera.NearClip, 0.01f);
			ImGui::DragFloat("Far Clip##camera", &camera.FarClip, 1.0f);
			if (ImGui::DragFloat("Aspect Ratio##camera", &camera.AspectRatio, 0.01f))
				camera.UpdateProjection();
		}

		if (entity.HasComponent<RigidbodyComponent>())
		{
			auto& rb = entity.GetComponent<RigidbodyComponent>();
			ImGui::Text("Rigidbody");
			const char* bodyTypes[] = { "Static", "Dynamic", "Kinematic" };
			int current = (int)rb.Type;
			if (ImGui::Combo("Body Type##rb", &current, bodyTypes, IM_ARRAYSIZE(bodyTypes)))
				rb.Type = (RigidbodyComponent::BodyType)current;
			ImGui::Checkbox("Fixed Rotation##rb", &rb.FixedRotation);
		}

		if (entity.HasComponent<BoxColliderComponent>())
		{
			auto& bc = entity.GetComponent<BoxColliderComponent>();
			ImGui::Text("Box Collider");
			ImGui::DragFloat3("Size##bc", &bc.Size.x, 0.01f);
			ImGui::DragFloat("Density##bc", &bc.Density, 0.01f);
			ImGui::DragFloat("Friction##bc", &bc.Friction, 0.01f);
			ImGui::DragFloat("Restitution##bc", &bc.Restitution, 0.01f);
		}

		if (entity.HasComponent<SphereColliderComponent>())
		{
			auto& sc = entity.GetComponent<SphereColliderComponent>();
			ImGui::Text("Sphere Collider");
			ImGui::DragFloat("Radius##sc", &sc.Radius, 0.01f);
			ImGui::DragFloat("Density##sc", &sc.Density, 0.01f);
			ImGui::DragFloat("Restitution##sc", &sc.Restitution, 0.01f);
		}

		if (entity.HasComponent<PBRMaterialComponent>())
		{
			auto& pbr = entity.GetComponent<PBRMaterialComponent>();
			ImGui::Text("PBR Material");
			ImGui::ColorEdit3("Albedo##pbr", &pbr.Albedo.x);
			ImGui::SliderFloat("Metallic##pbr", &pbr.Metallic, 0.0f, 1.0f);
			ImGui::SliderFloat("Roughness##pbr", &pbr.Roughness, 0.0f, 1.0f);
		}

		if (entity.HasComponent<LightComponent>())
		{
			auto& light = entity.GetComponent<LightComponent>();
			ImGui::Text("Light");
			const char* lightTypes[] = { "Directional", "Point", "Spot" };
			int current = (int)light.Type;
			if (ImGui::Combo("Type##light", &current, lightTypes, IM_ARRAYSIZE(lightTypes)))
				light.Type = (decltype(light.Type))current;
			ImGui::ColorEdit3("Color##light", &light.Color.x);
			ImGui::SliderFloat("Intensity##light", &light.Intensity, 0.0f, 50.0f);
		}

		if (entity.HasComponent<AudioSourceComponent>())
		{
			auto& audio = entity.GetComponent<AudioSourceComponent>();
			ImGui::Text("Audio Source");

			// Kept in a member buffer rather than a local: InputText needs stable
			// storage across frames, and a local would be reinitialised from the
			// component each frame, discarding anything mid-edit.
			CopyToBuffer(m_AudioPathBuffer, sizeof(m_AudioPathBuffer), audio.SourcePath);
			if (ImGui::InputText("Clip##audio", m_AudioPathBuffer, sizeof(m_AudioPathBuffer)))
				audio.SourcePath = m_AudioPathBuffer;

			ImGui::SliderFloat("Volume##audio", &audio.Volume, 0.0f, 1.0f);
			ImGui::DragFloat("Pitch##audio", &audio.Pitch, 0.01f, 0.01f, 8.0f);
			ImGui::Checkbox("Loop##audio", &audio.Loop);
			ImGui::Checkbox("Play On Start##audio", &audio.PlayOnStart);

			// Buses are named rather than numeric so the choice survives a
			// reordering of the enum and matches what the Lua and scene files use.
			const char* busNames[] = { "Master", "Music", "SFX", "Voice", "UI" };
			int currentBus = (int)audio.Bus;
			if (currentBus < 0 || currentBus >= (int)AudioBus::Count)
				currentBus = (int)AudioBus::SoundEffects;
			if (ImGui::Combo("Bus##audio", &currentBus, busNames, IM_ARRAYSIZE(busNames)))
				audio.Bus = (AudioBus)currentBus;

			ImGui::Separator();
			ImGui::Checkbox("Spatial (3D)##audio", &audio.Spatial);

			// Distances are meaningless without spatialisation, so they are only
			// shown when it is on rather than being editable and inert.
			if (audio.Spatial)
			{
				ImGui::DragFloat("Min Distance##audio", &audio.MinDistance, 0.1f, 0.0f, 1000.0f);
				ImGui::DragFloat("Max Distance##audio", &audio.MaxDistance, 0.1f, 0.0f, 1000.0f);

				// The engine clamps MaxDistance up to just past MinDistance, so an
				// inverted pair does not silence the source. It collapses the
				// falloff to a sliver though: full volume everywhere inside
				// MinDistance, inaudible immediately outside it - a cliff rather
				// than a ramp. Corrected here where the author can see it.
				if (audio.MaxDistance < audio.MinDistance)
					audio.MaxDistance = audio.MinDistance;
			}
		}

		if (entity.HasComponent<ScriptComponent>())
		{
			ImGui::Text("Script");
			char scriptBuffer[128];
			CopyToBuffer(scriptBuffer, sizeof(scriptBuffer), entity.GetComponent<ScriptComponent>().ClassName);
			if (ImGui::InputText("Class Name##script", scriptBuffer, sizeof(scriptBuffer)))
				entity.GetComponent<ScriptComponent>().ClassName = scriptBuffer;
		}

		if (entity.HasComponent<NativeScriptComponent>())
		{
			ImGui::Text("Native Script");
			char nativeBuffer[128];
			CopyToBuffer(nativeBuffer, sizeof(nativeBuffer), entity.GetComponent<NativeScriptComponent>().ClassName);
			if (ImGui::InputText("Class Name##native", nativeBuffer, sizeof(nativeBuffer)))
				entity.GetComponent<NativeScriptComponent>().ClassName = nativeBuffer;

			// The scripts this binary knows. The editor and a game are
			// different binaries, so this list is the editor's: a name that
			// is not in it can still be typed, because the scene it is saved
			// in is the game's, and the game's binary has it.
			const auto nativeNames = Forge::NativeScriptRegistry::GetNames();
			if (!nativeNames.empty())
			{
				if (ImGui::BeginCombo("##nativescript", entity.GetComponent<NativeScriptComponent>().ClassName.c_str()))
				{
					for (const auto& nativeName : nativeNames)
					{
						if (ImGui::Selectable(nativeName.c_str(), entity.GetComponent<NativeScriptComponent>().ClassName == nativeName))
							entity.GetComponent<NativeScriptComponent>().ClassName = nativeName;
					}
					ImGui::EndCombo();
				}
			}
		}

		// The panel is done: whatever the edit changed is now a change that
		// can be taken back.
		m_Editor.CommitEntityEdit();

		ImGui::End();
	}

	// Adds a component the entity does not already have, or removes one it does.
	//
	// ID, Tag and Transform are structural: the engine assumes every entity has
	// them (GetName, GetUUID and the gizmo all read them unconditionally), so
	// removing one would leave the entity in a state nothing else handles. They
	// are excluded from removal and never offered for addition.
	template<typename T>
	static void OfferAddComponent(Entity& entity, const char* label)
	{
		if (entity.HasComponent<T>())
			return;

		if (ImGui::MenuItem(label))
			entity.AddComponent<T>();
	}

	template<typename T>
	static void OfferRemoveComponent(Entity& entity, const char* label)
	{
		if (!entity.HasComponent<T>())
			return;

		if (ImGui::MenuItem(label))
			entity.RemoveComponent<T>();
	}

	void EditorApp::DrawComponentMenu(Entity& entity)
	{
		if (ImGui::BeginMenu("Add Component"))
		{
			OfferAddComponent<CameraComponent>(entity, "Camera");
			OfferAddComponent<LightComponent>(entity, "Light");
			OfferAddComponent<PBRMaterialComponent>(entity, "PBR Material");
			OfferAddComponent<MeshComponent>(entity, "Mesh");
			OfferAddComponent<ScriptComponent>(entity, "Script");
			OfferAddComponent<AudioSourceComponent>(entity, "Audio Source");
			OfferAddComponent<EnvironmentMapComponent>(entity, "Environment Map");
			OfferAddComponent<LabelComponent>(entity, "Label");
			OfferAddComponent<MultiMeshComponent>(entity, "Multi Mesh");

			// A native script is added by name, because a name is what a
			// scene holds. The names are the scripts this binary has.
			if (!entity.HasComponent<NativeScriptComponent>() && ImGui::BeginMenu("Native Script"))
			{
				const auto nativeNames = Forge::NativeScriptRegistry::GetNames();
				if (nativeNames.empty())
					ImGui::TextDisabled("No native scripts registered");

				for (const auto& nativeName : nativeNames)
				{
					if (ImGui::MenuItem(nativeName.c_str()))
						entity.AddComponent<NativeScriptComponent>().ClassName = nativeName;
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			OfferAddComponent<RigidbodyComponent>(entity, "Rigidbody");
			OfferAddComponent<BoxColliderComponent>(entity, "Box Collider");
			OfferAddComponent<SphereColliderComponent>(entity, "Sphere Collider");

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("Remove Component"))
		{
			OfferRemoveComponent<CameraComponent>(entity, "Camera");
			OfferRemoveComponent<LightComponent>(entity, "Light");
			OfferRemoveComponent<PBRMaterialComponent>(entity, "PBR Material");
			OfferRemoveComponent<MeshComponent>(entity, "Mesh");
			OfferRemoveComponent<ScriptComponent>(entity, "Script");
			OfferRemoveComponent<AudioSourceComponent>(entity, "Audio Source");
			OfferRemoveComponent<EnvironmentMapComponent>(entity, "Environment Map");
			OfferRemoveComponent<LabelComponent>(entity, "Label");
			OfferRemoveComponent<MultiMeshComponent>(entity, "Multi Mesh");
			OfferRemoveComponent<NativeScriptComponent>(entity, "Native Script");
			ImGui::Separator();
			OfferRemoveComponent<RigidbodyComponent>(entity, "Rigidbody");
			OfferRemoveComponent<BoxColliderComponent>(entity, "Box Collider");
			OfferRemoveComponent<SphereColliderComponent>(entity, "Sphere Collider");

			ImGui::EndMenu();
		}
	}

	void EditorApp::DrawViewportPanel()
	{
		ImGui::Begin("Viewport");

		// The camera's projection has to be built for the panel it draws into.
		// It was fixed at 16:9 for the life of the editor, while gizmo picking
		// used the panel's real size - so every drag picked somewhere other
		// than where the gizmo was drawn.
		const ImVec2 panelSize = ImGui::GetContentRegionAvail();
		m_Editor.GetCamera().SetViewportSize(panelSize.x, panelSize.y);

		// The scene cameras' projections follow the same panel: the simulate view
		// renders through a scene camera, and a 16:9 projection shown in a 3:1
		// panel is a stretched view.
		if (panelSize.x > 0.0f && panelSize.y > 0.0f)
			m_Editor.GetScene()->OnViewportResize((uint32_t)panelSize.x, (uint32_t)panelSize.y);

		if (void* textureView = Renderer::GetCompositedTextureView())
		{
			ImVec2 size = ImGui::GetContentRegionAvail();
			if (size.x > 0.0f && size.y > 0.0f)
			{
				if ((uint32_t)size.x != m_Viewport.GetWidth() || (uint32_t)size.y != m_Viewport.GetHeight())
					m_Viewport.Resize((uint32_t)size.x, (uint32_t)size.y);

				// Register the composited frame with ImGui. The ImGui Vulkan
				// backend requires a VkDescriptorSet (not a raw object pointer)
				// as ImTextureID. The tonemap view is recreated with the
				// targets (resize, render scale), so a view that changed gets
				// re-registered rather than sampled as a freed handle.
				const VkImageView view = static_cast<VkImageView>(textureView);
				if (view != m_ViewportTextureView)
				{
					if (m_ViewportTextureId)
						ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_ViewportTextureId));
					m_ViewportTextureView = view;
					m_ViewportTextureId = ImTextureID(
						(void*)ImGui_ImplVulkan_AddTexture(m_ViewportSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
				}

				ImGui::Image(m_ViewportTextureId, size);

				// The image's rectangle is the viewport: the ray a click
				// builds is unprojected through this rectangle, so the
				// picture and the pick have to agree on where the picture
				// is.
				m_ViewportImageSize = ImGui::GetItemRectSize();

				// The pointer over the picture is the pointer over the 3D
				// view, and that is where the camera flies from.
				m_PointerOverViewport = ImGui::IsItemHovered();

				// A click here selects what is under it; a drag moves the
				// selection.
				HandleViewportInteraction();
			}
		}
		else
		{
			ImGui::Text("Viewport not initialized");
			m_PointerOverViewport = false;
			m_ViewportImageSize = ImVec2(0.0f, 0.0f);
		}

		ImGui::End();
	}

	void EditorApp::DrawAssetBrowserPanel()
	{
		ImGui::Begin("Assets");

		if (ImGui::Button("Refresh"))
		{
			// An empty root scans the active project's asset directory.
		AssetRegistry::Scan();
			WatchWatchedAssets();
		}

		ImGui::Separator();

		for (const AssetMetadata& asset : AssetRegistry::GetAllAssets())
		{
			ImGui::PushID(asset.Path.c_str());
			ImGui::Text("%s  [%s]", asset.Name.c_str(), AssetTypeToString(asset.Type));

			// Double-click a prefab/scene to open it.
			if (asset.Type == AssetType::Scene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
			{
				const bool opened = m_Editor.OpenScene(asset.Path);
				if (opened)
					m_CurrentScenePath = asset.Path;
				Notify(opened ? "Opened " + asset.Name : "Failed to open " + asset.Path, !opened);
			}

			if (asset.Type == AssetType::Scene && ImGui::BeginPopupContextItem("open"))
			{
				if (ImGui::MenuItem("Open Scene"))
				{
					const bool opened = m_Editor.OpenScene(asset.Path);
					if (opened)
						m_CurrentScenePath = asset.Path;
					Notify(opened ? "Opened " + asset.Name : "Failed to open " + asset.Path, !opened);
				}
				ImGui::EndPopup();
			}

			ImGui::PopID();
		}

		ImGui::End();
	}

	void EditorApp::DrawPropertiesPanel()
	{
		ImGui::Begin("Properties");

		// The Hazel toolbar's state machine: Play runs the scripts under the
		// author's camera, Simulate runs them on the scene's own camera, and each
		// button becomes Stop while its own mode is live.
		const bool playLive = m_Editor.IsPlaying() && !m_Editor.IsSimulate();
		if (ImGui::Button(playLive ? "Stop" : "Play"))
		{
			if (playLive)
				m_Editor.Stop();
			else
				m_Editor.Play();
		}
		ImGui::SameLine();
		if (ImGui::Button(m_Editor.IsSimulate() ? "Stop" : "Simulate"))
		{
			if (m_Editor.IsSimulate())
				m_Editor.Stop();
			else
				m_Editor.Simulate();
		}
		ImGui::SameLine();
		if (ImGui::Button(m_Editor.IsPlaying() && !m_Editor.IsPaused() ? "Pause" : "Resume") && m_Editor.IsPlaying())
			m_Editor.Pause();

		ImGui::SameLine();
		if (ImGui::Button("Step") && m_Editor.IsPlaying())
			m_Editor.Step(1);

		ImGui::Separator();
		ImGui::Text("Entities: %d", (int)m_Editor.GetAllEntities().size());
		ImGui::Text("Selected: %s", m_Editor.HasSelection() ? m_Editor.GetSelectedEntity().GetName().c_str() : "None");

		ImGui::Separator();

		// Per-frame draw statistics (the Hazel stats panel): what the last frame
		// actually submitted, including what culling removed. A frame quietly
		// drawing the whole scene every time is invisible from the viewport.
		const RendererStatistics stats = Renderer::GetStatistics();
		ImGui::Text("Draw calls: %u  Culled: %u", stats.DrawCalls3D, stats.FrustumCulled);
		ImGui::Text("Triangles: %u  Textures bound: %u", stats.TrianglesSubmitted, stats.TexturesBound);

		ImGui::Separator();

		// Bloom (the Hazel post chain): glow is added to the HDR radiance
		// before tonemapping, so 0 turns the effect off at the composite -
		// the half-res chain is cheap enough to always record.
		float bloom = Renderer::GetBloomIntensity();
		ImGui::Text("Bloom:");
		ImGui::SameLine();
		if (ImGui::SliderFloat("##bloom", &bloom, 0.0f, 0.5f, "%.3f"))
			Renderer::SetBloomIntensity(bloom);
		ImGui::SameLine();
		if (ImGui::Button("Off"))
			Renderer::SetBloomIntensity(0.0f);

		ImGui::Separator();

		// Profiling controls (the Hazel editor pattern): the session is
		// explicit, the trace opens in chrome://tracing or Perfetto.
		if (!Forge::IsProfilingEnabled())
		{
			ImGui::TextDisabled("Profiler compiled out (Dist build)");
		}
		else
		{
			ImGui::Text("Profiler:");
			ImGui::SameLine();
			if (IsProfilingActive())
			{
				ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "recording");
				ImGui::SameLine();
				if (ImGui::Button("Stop Profiling"))
				{
					ProfileEndSession();
					m_Notification = "Profiling stopped";
					m_NotificationIsError = false;
					m_NotificationFramesRemaining = kNotificationFrames;
				}
			}
			else
			{
				ImGui::TextDisabled("idle");
				ImGui::SameLine();
				if (ImGui::Button("Start Profiling"))
				{
					const bool started = ProfileBeginSession("Editor Profile Session", "editor_profile.json");
					m_Notification = started
						? "Profiling to editor_profile.json"
						: "Profiling failed: could not open editor_profile.json";
					m_NotificationIsError = !started;
					m_NotificationFramesRemaining = kNotificationFrames;
				}
			}
		}

		ImGui::End();
	}

	// The live profiling view (the Hazel overlay panel): the instrumentor's
	// per-scope aggregation, heaviest first, updated every frame. It reads the
	// same measurements the trace file records, so the panel and the trace
	// cannot disagree about what the frame cost.
	void EditorApp::DrawProfilerPanel()
	{
		ImGui::Begin("Profiler");

		if (!Forge::IsProfilingEnabled())
		{
			ImGui::TextDisabled("Profiler compiled out (Dist build)");
			ImGui::End();
			return;
		}

		ImGui::Text("%s", IsProfilingActive() ? "Recording" : "Idle - start profiling in the Properties panel");

		const ProfileSnapshot snapshot = Forge::GetProfileSnapshot();
		ImGui::Text("Samples: %llu", (unsigned long long)snapshot.Samples);

		ImGui::Separator();

		if (snapshot.Scopes.empty())
		{
			ImGui::TextDisabled("No samples recorded");
		}
		else
		{
			if (ImGui::BeginTable("scopes", 5,
				ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV))
			{
				ImGui::TableSetupColumn("Scope");
				ImGui::TableSetupColumn("Samples");
				ImGui::TableSetupColumn("Total ms");
				ImGui::TableSetupColumn("Mean ms");
				ImGui::TableSetupColumn("Max ms");
				ImGui::TableHeadersRow();

				for (const ProfileScopeStats& scope : snapshot.Scopes)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(scope.Name.c_str());
					ImGui::TableNextColumn(); ImGui::Text("%llu", (unsigned long long)scope.Samples);
					ImGui::TableNextColumn(); ImGui::Text("%.2f", scope.TotalMillis);
					ImGui::TableNextColumn(); ImGui::Text("%.3f", scope.MeanMillis());
					ImGui::TableNextColumn(); ImGui::Text("%.3f", scope.MaxMillis);
				}

				ImGui::EndTable();
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Reset Stats"))
			Forge::ResetProfileStats();

		ImGui::End();
	}

	void EditorApp::ViewportRay(int x, int y, glm::vec3& origin, glm::vec3& direction)
	{
		// The editor camera's projection is sized to the viewport panel (see
		// DrawViewportPanel), so the unprojection below is the same mapping the
		// frame was drawn with.
		//
		// The pixel is counted in the *image's* own pixels: the image is the
		// picture the ray has to cross, and its size is the picture's size.
		// The panel's content region was used here before, which is the same
		// number for as long as the image fills the panel - and the wrong
		// number the moment it does not.
		const glm::mat4 vp = m_Editor.GetCamera().GetViewProjectionMatrix();
		if (m_ViewportImageSize.x <= 0.0f || m_ViewportImageSize.y <= 0.0f)
		{
			origin = glm::vec3(0.0f);
			direction = glm::vec3(0.0f, 0.0f, -1.0f);
			return;
		}

		const float ndcX = (2.0f * x / m_ViewportImageSize.x) - 1.0f;
		const float ndcY = 1.0f - (2.0f * y / m_ViewportImageSize.y);

		const glm::mat4 invVP = glm::inverse(vp);
		glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		glm::vec4 farPoint = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;

		origin = glm::vec3(nearPoint);
		direction = glm::normalize(glm::vec3(farPoint - nearPoint));
	}

	void EditorApp::PickAtScreenPoint(int x, int y)
	{
		glm::vec3 origin, direction;
		ViewportRay(x, y, origin, direction);

		// The selection is left alone when nothing is hit; a click is often the
		// end of a camera move, and losing the selection at the end of a drag
		// is not what anyone asked for. The answer is intentionally not
		// surfaced as a notification - a miss is not an event.
		if (m_Editor.SelectEntityAtRay(origin, direction))
			FG_CORE_INFO("Viewport: selected '{0}'", m_Editor.GetSelectedEntity().GetName());
	}

	bool EditorApp::ProjectToViewport(const glm::vec3& world, ImVec2& outLocal)
	{
		if (m_ViewportImageSize.x <= 0.0f || m_ViewportImageSize.y <= 0.0f)
			return false;

		const glm::mat4 vp = m_Editor.GetCamera().GetViewProjectionMatrix();
		const glm::vec4 clip = vp * glm::vec4(world, 1.0f);
		if (clip.w <= 0.0f)
			return false;  // behind the camera: no place on the picture for it

		const glm::vec3 ndc = glm::vec3(clip) / clip.w;
		outLocal = ImVec2(
			(ndc.x * 0.5f + 0.5f) * m_ViewportImageSize.x,
			(0.5f - ndc.y * 0.5f) * m_ViewportImageSize.y);
		return true;
	}

	bool EditorApp::IsNearSelectedEntity(float x, float y)
	{
		if (!m_Editor.HasSelection())
			return false;

		Entity entity = m_Editor.GetSelectedEntity();
		if (!entity.HasComponent<TransformComponent>())
			return false;

		ImVec2 local;
		if (!ProjectToViewport(entity.GetComponent<TransformComponent>().Translation, local))
			return false;

		// The gizmo is at the entity, so a drag has to start near it. A drag
		// that starts anywhere in the viewport would move whatever happens to
		// be selected, and dragging across empty space is not a request to
		// move things.
		const float dx = x - local.x;
		const float dy = y - local.y;
		return (dx * dx + dy * dy) <= static_cast<float>(kGizmoGrabRadius * kGizmoGrabRadius);
	}

	bool EditorApp::OnGizmoMouseDown(int button, int x, int y)
	{
		if (!m_Editor.HasSelection())
			return false;

		Entity entity = m_Editor.GetSelectedEntity();
		if (!entity.HasComponent<TransformComponent>())
			return false;

		// Get the entity's world position.
		auto& tc = entity.GetComponent<TransformComponent>();
		glm::vec3 worldPos = tc.Translation;

		ViewportRay(x, y, g_GizmoRayOrigin, g_GizmoRayDirection);
		g_GizmoButton = button;
		g_GizmoAxis = -1;
		g_GizmoStartPos = worldPos;
		g_GizmoStartValue = worldPos;

		// Which axis did the author take hold of? The one whose line passes
		// nearest the cursor.
		//
		// The rule this replaces picked the axis most parallel to the *view
		// direction*, which names the axis pointing away from the camera - the
		// one an author is least likely to be reaching for, and the one whose
		// movement is hardest to see.
		glm::vec3 axes[] = { glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) };
		float closest = 0.0f;
		for (int i = 0; i < 3; ++i)
		{
			glm::vec3 point;
			float distance = 0.0f;
			if (!Gizmo::ClosestPointOnAxis(g_GizmoRayOrigin, g_GizmoRayDirection, worldPos, axes[i], point, distance))
				continue;  // the cursor looks straight down this axis

			if (g_GizmoAxis < 0 || distance < closest)
			{
				closest = distance;
				g_GizmoAxis = i;
			}
		}

		if (g_GizmoAxis >= 0)
		{
			FG_CORE_INFO("Gizmo: started dragging axis {0} at ({1}, {2}, {3})",
				g_GizmoAxis, worldPos.x, worldPos.y, worldPos.z);
			return true;
		}

		// No axis at all: the drag has nothing to drag.
		g_GizmoButton = -1;
		return false;
	}

	void EditorApp::OnGizmoMouseMove(int x, int y)
	{
		if (g_GizmoButton < 0 || g_GizmoAxis < 0)
			return;

		Entity entity = m_Editor.GetSelectedEntity();
		if (!entity.HasComponent<TransformComponent>())
			return;

		glm::vec3 rayOrigin, rayDir;
		ViewportRay(x, y, rayOrigin, rayDir);

		// Get the axis direction (local or world space).
		glm::vec3 axisDir = glm::vec3(0);
		switch (g_GizmoAxis)
		{
		case 0: axisDir = glm::vec3(1, 0, 0); break;
		case 1: axisDir = glm::vec3(0, 1, 0); break;
		case 2: axisDir = glm::vec3(0, 0, 1); break;
		}

		// Where the cursor's ray passes closest to the axis: that is where the
		// entity follows it to. The old maths projected the *drag start* ray
		// onto the axis and mixed it with the current direction, which moved
		// the entity by an amount that had nothing to do with the cursor.
		glm::vec3 closestPoint;
		float distance = 0.0f;
		if (!Gizmo::ClosestPointOnAxis(rayOrigin, rayDir, g_GizmoStartPos, axisDir, closestPoint, distance))
			return;  // the ray runs along the axis: nothing to follow

		// The movement along the axis, measured from where the drag started.
		const float along = glm::dot(closestPoint - g_GizmoStartPos, axisDir);
		glm::vec3 newPos = g_GizmoStartPos + axisDir * along;

		// Apply snap if enabled.
		float snap = m_Editor.GetTranslationSnap();
		if (snap > 0.0f)
		{
			newPos = Gizmo::Snap(newPos, snap);
		}

		// Update the entity's transform.
		entity.GetComponent<TransformComponent>().Translation = newPos;

		// The change is undoable, and a drag is one change: the commands of
		// a drag merge with each other, so the history holds the position
		// the entity started at and the position it ended at.
		const TransformComponent& current = entity.GetComponent<TransformComponent>();
		m_Editor.RecordTransformChange(entity, g_GizmoStartPos, current.Rotation, current.Scale);
	}

	void EditorApp::OnGizmoMouseUp(int /*button*/, int /*x*/, int /*y*/)
	{
		g_GizmoButton = -1;
		g_GizmoAxis = -1;
	}

	void EditorApp::HandleViewportInteraction()
	{
		// The image is the thing the pointer is over, so its rectangle is the
		// viewport: the origin is its top-left corner and its size is its
		// size. Everything below is counted in the image's own pixels.
		const ImVec2 rectMin = ImGui::GetItemRectMin();
		const ImVec2 rectMax = ImGui::GetItemRectMax();
		const ImVec2 mouse = ImGui::GetMousePos();

		const float mx = mouse.x - rectMin.x;
		const float my = mouse.y - rectMin.y;
		const bool inside = mx >= 0.0f && my >= 0.0f
			&& mx <= rectMax.x - rectMin.x && my <= rectMax.y - rectMin.y;

		if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			g_MouseDownX = static_cast<int>(mx);
			g_MouseDownY = static_cast<int>(my);
			g_PressedInViewport = true;
		}

		// A drag is a drag and a click is a click. The drag only begins once
		// the pointer has moved, so that a click on an entity selects it
		// instead of nudging it along an axis.
		if (g_PressedInViewport && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !g_DragStarted)
		{
			const int dx = static_cast<int>(mx) - g_MouseDownX;
			const int dy = static_cast<int>(my) - g_MouseDownY;
			if (glm::abs(dx) > kDragThreshold || glm::abs(dy) > kDragThreshold)
			{
				// The drag has to have started on the gizmo: the gizmo is
				// at the entity, and a drag that started anywhere else is
				// not a drag of anything.
				const bool grabbed = IsNearSelectedEntity(static_cast<float>(g_MouseDownX), static_cast<float>(g_MouseDownY));
				g_DragStarted = grabbed && OnGizmoMouseDown(0, static_cast<int>(mx), static_cast<int>(my));
			}
		}

		if (g_DragStarted)
			OnGizmoMouseMove(static_cast<int>(mx), static_cast<int>(my));

		if (g_PressedInViewport && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			if (g_DragStarted)
			{
				OnGizmoMouseUp(0, static_cast<int>(mx), static_cast<int>(my));
			}
			else if (inside)
			{
				// A click that did not drag is a request to select what is
				// under the cursor.
				PickAtScreenPoint(static_cast<int>(mx), static_cast<int>(my));
			}

			g_DragStarted = false;
			g_PressedInViewport = false;
		}
	}

	void EditorApp::WatchWatchedAssets()
	{
		m_AssetWatcher.Clear();

		// The renderer reloads texture assets through its own signature
		// check; the watcher is what tells the editor (and the log) that the
		// file on disk changed at all. Scripts are watched too, because a
		// changed .lua is hot-reloaded through ScriptEngine below.
		for (AssetType type : { AssetType::Texture, AssetType::HDR, AssetType::Script })
		{
			for (const AssetMetadata& asset : AssetRegistry::GetAssetsOfType(type))
				m_AssetWatcher.Watch(asset.Path);
		}
	}

	void EditorApp::PollAssetHotReloads(float dt)
	{
		m_AssetPollAccumulator += dt;
		if (m_AssetPollAccumulator < 0.5f)
			return;
		m_AssetPollAccumulator = 0.0f;

		const auto events = m_AssetWatcher.Poll();
		if (events.empty())
			return;

		for (const auto& event : events)
		{
			FG_CORE_INFO("Asset {0}: {1}", AssetWatcher::ChangeToString(event.Kind), event.Path);
			m_Notification = event.Path + " " + AssetWatcher::ChangeToString(event.Kind);
			m_NotificationIsError = false;
			m_NotificationFramesRemaining = kNotificationFrames;

			// Hot reload only matters while a runtime session is live;
			// outside playback the script is (re)loaded on the next Play.
			if (ClassifyAsset(event.Path) == AssetType::Script &&
				ScriptEngine::IsRuntimeActive())
			{
				const bool reloaded = ScriptEngine::HotReloadScript(event.Path);
				m_Notification = reloaded
					? "Hot reloaded " + event.Path
					: "Hot reload FAILED for " + event.Path + " - previous script keeps running";
				m_NotificationIsError = !reloaded;
				m_NotificationFramesRemaining = kNotificationFrames;
			}
		}

		// New or removed files change the registry itself; re-scan and watch
		// whatever is there now so appearances are reported too.
		// An empty root scans the active project's asset directory.
		AssetRegistry::Scan();
		WatchWatchedAssets();
	}

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new EditorApp(args);
	}

}