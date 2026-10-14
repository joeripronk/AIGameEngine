#include "fgpch.h"
#include "editor/src/EditorApp.h"

#include "Forge/Core/Log.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Assets/AssetRegistry.h"
#include "Forge/Scene/Components.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

namespace Forge {

	EditorApp::EditorApp()
		: Application(ApplicationSpecification{ "Forge Editor", "", { 0, nullptr } })
	{
		Renderer::Init();

		CreateDefaultScene();

		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(GetWindow().GetNativeWindow()), true);

		m_Editor.GetCamera().SetPosition({ 0.0f, 5.0f, 12.0f });
		m_Editor.GetCamera().SetRotation({ 20.0f, 0.0f, 0.0f });

		m_Viewport.Init(800, 600);
	}

	EditorApp::~EditorApp()
	{
		m_Viewport.Shutdown();
		ImGui_ImplGlfw_Shutdown();
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

		float lastFrameTime = 0.0f;
		while (IsRunning())
		{
			float time = (float)glfwGetTime();
			Timestep timestep = time - lastFrameTime;
			lastFrameTime = time;

			if (!IsMinimized())
			{
				for (Layer* layer : GetLayerStack())
					layer->OnUpdate(timestep);

				SyncEditorCamera();
				m_Editor.Update(timestep);
			}

			GetWindowScope()->OnUpdate();
			ExecuteMainThreadQueue();

			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			DrawMenuBar();
			DrawGizmoToolbar();
			DrawHierarchyPanel();
			DrawInspectorPanel();
			DrawViewportPanel();
			DrawAssetBrowserPanel();
			DrawPropertiesPanel();

			ImGui::Render();
			// TODO: present the ImGui draw data to the Vulkan swapchain.
		}
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
				m_Editor.SaveScene("resources/scenes/editor.forge");

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

				ImGui::Image(
					ImTextureID((void*)(uintptr_t)m_Viewport.GetRenderTexture().get()),
					size);
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
			AssetRegistry::Scan("resources/");

		ImGui::Separator();

		for (const AssetMetadata& asset : AssetRegistry::GetAllAssets())
		{
			ImGui::PushID(asset.Path.c_str());
			ImGui::Text("%s  [%s]", asset.Name.c_str(), AssetTypeToString(asset.Type));

			// Double-click a prefab/scene to open it.
			if (asset.Type == AssetType::Scene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
				m_Editor.OpenScene(asset.Path);

			if (asset.Type == AssetType::Scene && ImGui::BeginPopupContextItem("open"))
			{
				if (ImGui::MenuItem("Open Scene"))
					m_Editor.OpenScene(asset.Path);
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

		ImGui::End();
	}

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		(void)args;
		return new EditorApp();
	}

}