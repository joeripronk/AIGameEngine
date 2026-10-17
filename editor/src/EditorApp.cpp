#include "fgpch.h"
#include "Forge/Debug/Instrumentor.h"
#include "editor/src/EditorApp.h"

#include "Forge/Core/Log.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Assets/AssetRegistry.h"
#include "Forge/Project/GameExporter.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scripting/ScriptEngine.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "Platform/Vulkan/VulkanRendererAPI.h"
#include "Platform/Vulkan/VulkanSwapchain.h"
#include "Platform/Vulkan/VulkanTexture.h"

#include <nvrhi/common/resource.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

namespace Forge {

	namespace {
		// Ray origin and direction in world space for gizmo interaction.
		glm::vec3 g_GizmoRayOrigin;
		glm::vec3 g_GizmoRayDirection;
		int g_GizmoButton = -1;
		int g_GizmoAxis = -1;  // 0=X, 1=Y, 2=Z
		glm::vec3 g_GizmoStartPos;
		glm::vec3 g_GizmoStartValue;
	}

	EditorApp::EditorApp()
		: Application(ApplicationSpecification{ "Forge Editor", "", { 0, nullptr } })
	{
		Renderer::Init();

		CreateDefaultScene();

		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(GetWindow().GetNativeWindow()), true);

		// Initialise the ImGui Vulkan backend with dynamic rendering (the engine
		// uses nvrhi's vk::RenderingInfo / beginRendering, not VkRenderPass).
		//
		// The internal descriptor pool is large enough for the font atlas + one
		// descriptor set per ImGui texture (viewport image).
		m_ImGuiVulkanInitInfo = {};
		m_ImGuiVulkanInitInfo.ApiVersion = VK_API_VERSION_1_3;
		m_ImGuiVulkanInitInfo.Instance = VulkanRendererAPI::GetInstance();
		m_ImGuiVulkanInitInfo.PhysicalDevice = VulkanRendererAPI::GetPhysicalDevice();
		m_ImGuiVulkanInitInfo.Device = VulkanRendererAPI::GetLogicalDevice();
		m_ImGuiVulkanInitInfo.QueueFamily = VulkanRendererAPI::GetGraphicsQueueIndex();
		m_ImGuiVulkanInitInfo.Queue = VulkanRendererAPI::GetGraphicsQueue();
		m_ImGuiVulkanInitInfo.PipelineCache = VK_NULL_HANDLE;
		m_ImGuiVulkanInitInfo.DescriptorPool = VK_NULL_HANDLE;
		m_ImGuiVulkanInitInfo.DescriptorPoolSize = 256;
		m_ImGuiVulkanInitInfo.Allocator = nullptr;
		m_ImGuiVulkanInitInfo.MinImageCount = VulkanRendererAPI::GetSwapchain().GetImageCount();
		m_ImGuiVulkanInitInfo.ImageCount = VulkanRendererAPI::GetSwapchain().GetImageCount();
		m_ImGuiVulkanInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		m_ImGuiVulkanInitInfo.UseDynamicRendering = true;
		m_ImGuiVulkanInitInfo.PipelineRenderingCreateInfo = {};
		m_ImGuiVulkanInitInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		m_ImGuiVulkanInitInfo.PipelineRenderingCreateInfo.pNext = nullptr;
		m_ImGuiVulkanInitInfo.PipelineRenderingCreateInfo.viewMask = 0;
		m_ImGuiVulkanInitInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
		m_ImGuiVulkanInitInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &m_ImGuiRenderFormat;
		m_ImGuiRenderFormat = VulkanRendererAPI::GetSwapchain().GetVkFormat();

		IM_ASSERT(ImGui_ImplVulkan_Init(&m_ImGuiVulkanInitInfo));

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
		VkDevice device = VulkanRendererAPI::GetLogicalDevice();
		if (device)
			vkCreateSampler(device, &samplerInfo, nullptr, &m_ViewportSampler);
	}

	EditorApp::~EditorApp()
	{
		ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		m_Viewport.Shutdown();

		if (m_ViewportSampler != VK_NULL_HANDLE)
		{
			VkDevice device = VulkanRendererAPI::GetLogicalDevice();
			if (device)
				vkDestroySampler(device, m_ViewportSampler, nullptr);
			m_ViewportSampler = VK_NULL_HANDLE;
		}

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
		AssetRegistry::Scan("resources/");
		WatchWatchedAssets();

		float lastFrameTime = 0.0f;
		while (IsRunning())
		{
			FG_PROFILE_SCOPE("Frame");
			float time = (float)glfwGetTime();
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
				Renderer::BeginSSAOPass();
				Renderer::BeginHDRPass();
				Renderer::BeginFinalPass();

				// Render ImGui into the command list BEFORE it is closed.
				// GetCurrentCommandBuffer() is only valid between BeginScene()
				// and EndScene().
				ImGui_ImplGlfw_NewFrame();
				ImGui_ImplVulkan_NewFrame();
				ImGui::NewFrame();

				DrawMenuBar();
				DrawGizmoToolbar();
				DrawGizmoInteraction();
				DrawHierarchyPanel();
				DrawInspectorPanel();
				DrawViewportPanel();
				DrawAssetBrowserPanel();
				DrawPropertiesPanel();

				// Drawn last so it sits above every other panel.
				DrawNotification();

				ImGui::Render();
				VkCommandBuffer cmdBuf = VulkanRendererAPI::GetCurrentCommandBuffer();
				if (cmdBuf != VK_NULL_HANDLE)
					ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);

				// EndFinalPass is a no-op (the final pass is the last render
				// pass; EndScene closes the command list).
				Renderer::EndScene();

				// Present the swapchain image.
				Renderer::PresentSwapchainImage(swapchainImage);
			}

			GetWindowScope()->OnUpdate();
			ExecuteMainThreadQueue();
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
			| ImGuiWindowFlags_NoNav;

		ImGui::Begin("##Notification", nullptr, flags);

		ImGui::PushStyleColor(ImGuiCol_Text, m_NotificationIsError
			? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
			: ImVec4(0.55f, 0.9f, 0.6f, 1.0f));
		ImGui::TextUnformatted(m_Notification.c_str());
		ImGui::PopStyleColor();

		if (ImGui::Button("Dismiss"))
		{
			m_Notification.clear();
			m_NotificationFramesRemaining = 0;
		}

		ImGui::End();

		--m_NotificationFramesRemaining;
	}

	void EditorApp::DrawMenuBar()
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("New Scene"))
				CreateDefaultScene();

			if (ImGui::MenuItem("Save Scene"))
			{
				const bool saved = m_Editor.SaveScene("resources/scenes/editor.forge");
				Notify(saved ? "Scene saved" : "Failed to save scene", !saved);
			}

			ImGui::Separator();
			if (ImGui::MenuItem("Export Game..."))
			{
				GameExporter::Options opts;
				opts.OutputDirectory = "dist/";
				opts.ScenePath = "resources/scenes/editor.forge";
				opts.ResourcesDirectory = "resources/";
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

	void EditorApp::DrawGizmoToolbar()
	{
		ImGui::Begin("Gizmo");
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

		char nameBuffer[256];
		strncpy(nameBuffer, entity.GetName().c_str(), sizeof(nameBuffer) - 1);
		nameBuffer[sizeof(nameBuffer) - 1] = '\0';
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

		if (entity.HasComponent<CameraComponent>())
		{
			ImGui::Checkbox("Primary Camera##camera", &entity.GetComponent<CameraComponent>().Primary);
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
			strncpy(m_AudioPathBuffer, audio.SourcePath.c_str(), sizeof(m_AudioPathBuffer) - 1);
			m_AudioPathBuffer[sizeof(m_AudioPathBuffer) - 1] = '\0';
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
			strncpy(scriptBuffer, entity.GetComponent<ScriptComponent>().ClassName.c_str(), sizeof(scriptBuffer) - 1);
			scriptBuffer[sizeof(scriptBuffer) - 1] = '\0';
			if (ImGui::InputText("Class Name##script", scriptBuffer, sizeof(scriptBuffer)))
				entity.GetComponent<ScriptComponent>().ClassName = scriptBuffer;
		}

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

		m_Viewport.Render(m_Editor.GetScene().get(), m_Editor.GetViewProjection());

		if (m_Viewport.GetRenderTexture())
		{
			ImVec2 size = ImGui::GetContentRegionAvail();
			if (size.x > 0.0f && size.y > 0.0f)
			{
				if ((uint32_t)size.x != m_Viewport.GetWidth() || (uint32_t)size.y != m_Viewport.GetHeight())
					m_Viewport.Resize((uint32_t)size.x, (uint32_t)size.y);

				// Register the viewport texture with ImGui if we haven't yet.
				// The VulkanTexture's native handles are used because the
				// ImGui Vulkan backend requires a VkDescriptorSet (not a raw
				// object pointer) as ImTextureID.
				if (!m_ViewportTextureId)
				{
					auto vulkanTex = std::dynamic_pointer_cast<VulkanTexture>(m_Viewport.GetRenderTexture());
					if (vulkanTex)
					{
						VkImageView view = vulkanTex->GetVkImageView();
						if (view != VK_NULL_HANDLE)
						{
							m_ViewportTextureId = ImTextureID(
								(void*)ImGui_ImplVulkan_AddTexture(m_ViewportSampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
						}
					}
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
			AssetRegistry::Scan("resources/");
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
				Notify(opened ? "Opened " + asset.Name : "Failed to open " + asset.Path, !opened);
			}

			if (asset.Type == AssetType::Scene && ImGui::BeginPopupContextItem("open"))
			{
				if (ImGui::MenuItem("Open Scene"))
				{
					const bool opened = m_Editor.OpenScene(asset.Path);
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

		bool playing = m_Editor.IsPlaying();
		ImGui::Checkbox("Playing", &playing);
		if (playing != m_Editor.IsPlaying())
			m_Editor.SetPlaying(playing);
		ImGui::SameLine();
		if (ImGui::Button(m_Editor.IsPlaying() ? "Stop" : "Play"))
			m_Editor.SetPlaying(!m_Editor.IsPlaying());

		ImGui::SameLine();
		if (ImGui::Button("Step") && m_Editor.IsPlaying())
			m_Editor.Step(1);

		ImGui::Separator();
		ImGui::Text("Entities: %d", (int)m_Editor.GetAllEntities().size());
		ImGui::Text("Selected: %s", m_Editor.HasSelection() ? m_Editor.GetSelectedEntity().GetName().c_str() : "None");

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

		// Get the editor camera's view-projection and viewport.
		auto& camera = m_Editor.GetCamera();
		glm::mat4 vp = camera.GetViewProjectionMatrix();

		// Convert screen coordinates to NDC.
		ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		float ndcX = (2.0f * x / viewportSize.x) - 1.0f;
		float ndcY = 1.0f - (2.0f * y / viewportSize.y);

		// Unproject to get ray in world space.
		glm::mat4 invVP = glm::inverse(vp);
		glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		glm::vec4 farPoint = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;

		g_GizmoRayOrigin = glm::vec3(nearPoint);
		g_GizmoRayDirection = glm::normalize(glm::vec3(farPoint - nearPoint));
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

		// Get the editor camera's view-projection and viewport.
		auto& camera = m_Editor.GetCamera();
		glm::mat4 vp = camera.GetViewProjectionMatrix();

		// Convert screen coordinates to NDC.
		ImVec2 viewportSize = ImGui::GetContentRegionAvail();
		float ndcX = (2.0f * x / viewportSize.x) - 1.0f;
		float ndcY = 1.0f - (2.0f * y / viewportSize.y);

		// Unproject to get ray in world space.
		glm::mat4 invVP = glm::inverse(vp);
		glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
		glm::vec4 farPoint = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
		nearPoint /= nearPoint.w;
		farPoint /= farPoint.w;

		glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));

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
			if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && g_GizmoButton < 0)
			{
				OnGizmoMouseDown(0, (int)ImGui::GetMousePos().x - cursorPos.x,
					(int)ImGui::GetMousePos().y - cursorPos.y);
			}

			if (g_GizmoButton >= 0)
			{
				OnGizmoMouseMove((int)ImGui::GetMousePos().x - cursorPos.x,
					(int)ImGui::GetMousePos().y - cursorPos.y);
			}

			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && g_GizmoButton >= 0)
			{
				OnGizmoMouseUp(0, (int)ImGui::GetMousePos().x - cursorPos.x,
					(int)ImGui::GetMousePos().y - cursorPos.y);
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
		AssetRegistry::Scan("resources/");
		WatchWatchedAssets();
	}

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		(void)args;
		return new EditorApp();
	}

}