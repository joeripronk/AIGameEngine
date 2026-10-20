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
		m_Editor.GetCamera().SetRotation({ 20.0f, 0.0f, 0.0f });

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
		camera.AddComponent<CameraComponent>().Primary = true;

		auto light = m_Editor.CreateEntity("DirectionalLight");
		auto& lightComponent = light.AddComponent<LightComponent>();
		lightComponent.Type = LightComponent::Type::Directional;
		lightComponent.Direction = { -0.5f, -1.0f, -0.3f };

		auto cube = m_Editor.CreateEntity("Cube");
		auto& cubeTransform = cube.GetComponent<TransformComponent>();
		cubeTransform.Translation = { 0.0f, 0.5f, 0.0f };
		cube.AddComponent<PBRMaterialComponent>().Albedo = { 0.8f, 0.3f, 0.3f };

		auto physics = m_Editor.CreateEntity("PhysicsBox");
		physics.GetComponent<TransformComponent>().Translation = { 0.0f, 5.0f, 0.0f };
		auto& rigidbody = physics.AddComponent<RigidbodyComponent>();
		rigidbody.Type = RigidbodyComponent::BodyType::Dynamic;
		physics.AddComponent<BoxColliderComponent>().Size = { 1.0f, 1.0f, 1.0f };

		auto scriptEntity = m_Editor.CreateEntity("ScriptEntity");
		scriptEntity.GetComponent<TransformComponent>().Translation = { 4.0f, 0.5f, 0.0f };
		scriptEntity.AddComponent<ScriptComponent>().ClassName = "Mover";

		m_Editor.SelectEntity(cube);
	}

	void EditorApp::SyncEditorCamera()
	{
		// Simulate is the opposite of syncing: the scene camera runs free and the
		// author watches it from their own fixed viewpoint.
		if (m_Editor.IsSimulate())
			return;

		// Follow the primary camera so the viewport matches the scene.
		auto cameraEntity = m_Editor.GetScene()->GetPrimaryCameraEntity();
		if (cameraEntity)
		{
			auto& tc = cameraEntity.GetComponent<TransformComponent>();
			m_Editor.GetCamera().SetPosition(tc.Translation);
			m_Editor.GetCamera().SetRotation(tc.Rotation);
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
					DrawGizmoInteraction();
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

		// Scoped, so the Dismiss early-return below cannot leave the colour
		// pushed into the next window drawn this frame.
		Forge::UI::ScopedStyleColor color(ImGuiCol_Text, m_NotificationIsError
			? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
			: ImVec4(0.55f, 0.9f, 0.6f, 1.0f));
		ImGui::TextUnformatted(m_Notification.c_str());

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
				m_Editor.SelectEntity(cube);
			}
			if (ImGui::MenuItem("Create Sphere"))
			{
				auto sphere = m_Editor.CreateEntity("Sphere");
				sphere.GetComponent<TransformComponent>().Translation = { 0.0f, 0.5f, 0.0f };
				sphere.AddComponent<PBRMaterialComponent>();
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
		if (ImGui::RadioButton("Move", m_Editor.GetGizmoMode() == Gizmo::Mode::Translate))
			m_Editor.SetGizmoMode(Gizmo::Mode::Translate);
		ImGui::SameLine();
		if (ImGui::RadioButton("Rotate", m_Editor.GetGizmoMode() == Gizmo::Mode::Rotate))
			m_Editor.SetGizmoMode(Gizmo::Mode::Rotate);
		ImGui::SameLine();
		if (ImGui::RadioButton("Scale", m_Editor.GetGizmoMode() == Gizmo::Mode::Scale))
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
			}
		}
		else
		{
			ImGui::Text("Viewport not initialized");
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
		const glm::mat4 vp = m_Editor.GetCamera().GetViewProjectionMatrix();
		const ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		const float ndcX = (2.0f * x / viewportSize.x) - 1.0f;
		const float ndcY = 1.0f - (2.0f * y / viewportSize.y);

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
		void(m_Editor.SelectEntityAtRay(origin, direction));
	}

	void EditorApp::OnGizmoMouseDown(int button, int x, int y)
	{
		if (!m_Editor.HasSelection())
			return;

		Entity entity = m_Editor.GetSelectedEntity();
		if (!entity.HasComponent<TransformComponent>())
			return;

		// Get the entity's world position.
		auto& tc = entity.GetComponent<TransformComponent>();
		glm::vec3 worldPos = tc.Translation;

		ViewportRay(x, y, g_GizmoRayOrigin, g_GizmoRayDirection);
		g_GizmoButton = button;
		g_GizmoAxis = -1;
		g_GizmoStartPos = worldPos;
		g_GizmoStartValue = worldPos;

		// Check which axis the mouse is over (simple axis picking).
		// For now, we'll pick the axis closest to the ray direction.
		glm::vec3 axes[] = { glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1) };
		float maxDot = 0.0f;
		for (int i = 0; i < 3; ++i)
		{
			float dot = glm::abs(glm::dot(g_GizmoRayDirection, axes[i]));
			if (dot > maxDot)
			{
				maxDot = dot;
				g_GizmoAxis = i;
			}
		}

		if (g_GizmoAxis >= 0)
		{
			FG_CORE_INFO("Gizmo: started dragging axis {0} at ({1}, {2}, {3})",
				g_GizmoAxis, worldPos.x, worldPos.y, worldPos.z);
		}
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

		// Project the ray onto the axis to get the movement amount.
		float dot = glm::dot(rayDir, axisDir);
		if (glm::abs(dot) < 0.001f)
			return;  // Ray is perpendicular to axis

		// Calculate the movement along the axis.
		glm::vec3 toRay = g_GizmoRayOrigin - g_GizmoStartPos;
		float t = glm::dot(toRay, axisDir) / dot;
		glm::vec3 newPos = g_GizmoStartPos + axisDir * t;

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

	void EditorApp::DrawGizmoInteraction()
	{
		if (!m_Editor.HasSelection())
			return;

		// Check if mouse is in the viewport area (simple heuristic).
		ImGui::SetCursorPos(ImVec2(0, 0));
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		ImVec2 viewportSize = ImGui::GetContentRegionAvail();

		ImGui::PushID("gizmo_interaction");
		ImGui::InvisibleButton("gizmo_area", viewportSize);

		if (ImGui::IsItemHovered())
		{
			const int mx = static_cast<int>(ImGui::GetMousePos().x - cursorPos.x);
			const int my = static_cast<int>(ImGui::GetMousePos().y - cursorPos.y);

			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				g_MouseDownX = mx;
				g_MouseDownY = my;
			}

			if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && g_GizmoButton < 0)
				OnGizmoMouseDown(0, mx, my);

			if (g_GizmoButton >= 0)
				OnGizmoMouseMove(mx, my);

			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
			{
				if (g_GizmoButton >= 0)
					OnGizmoMouseUp(0, mx, my);
				else if (glm::abs(mx - g_MouseDownX) <= 4 && glm::abs(my - g_MouseDownY) <= 4)
					PickAtScreenPoint(mx, my);
			}
		}

		ImGui::PopID();
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