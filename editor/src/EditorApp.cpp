#include "fgpch.h"
#include "editor/src/EditorApp.h"

#include "Forge/Core/Log.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Assets/AssetRegistry.h"
#include "Forge/Scene/Components.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "Platform/Vulkan/VulkanRendererAPI.h"

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
			DrawGizmoInteraction();
			DrawHierarchyPanel();
			DrawInspectorPanel();
			DrawViewportPanel();
			DrawAssetBrowserPanel();
			DrawPropertiesPanel();

			ImGui::Render();
			VkCommandBuffer cmdBuf = VulkanRendererAPI::GetCurrentCommandBuffer();
			if (cmdBuf != VK_NULL_HANDLE)
				ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
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

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		(void)args;
		return new EditorApp();
	}

}