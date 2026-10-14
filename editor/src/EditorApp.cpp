#include "fgpch.h"
#include "editor/src/EditorApp.h"

#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Scene/SceneSerializer.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

namespace Forge {

	EditorApp::EditorApp()
		: Application(ApplicationSpecification{ "Forge Editor", "", {0, nullptr} })
	{
		// Initialize renderer
		Renderer::Init();

		// Create default scene
		CreateDefaultScene();

		// Setup ImGui
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

		// ImGui::StyleColorsDark(); // Commented out for WIP version compatibility

		ImGui_ImplGlfw_InitForVulkan(static_cast<GLFWwindow*>(GetWindow().GetNativeWindow()), true);

		// Setup Vulkan context for ImGui (simplified - no actual Vulkan rendering)
		ImGuiIO& io2 = ImGui::GetIO();
		(void)io2;

		// Setup initial camera
		m_Camera.SetPosition({ 0.0f, 5.0f, 10.0f });
		m_Camera.SetRotation({ 0.0f, 0.0f, 0.0f });
	}

	EditorApp::~EditorApp()
	{
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	}

	void EditorApp::CreateDefaultScene()
	{
		m_Scene = CreateRef<Scene>();

		// Create a ground plane
		auto ground = m_Scene->CreateEntity("Ground");
		auto& groundTransform = ground.AddComponent<TransformComponent>();
		groundTransform.Translation = { 0.0f, -1.0f, 0.0f };
		groundTransform.Scale = { 10.0f, 1.0f, 10.0f };

		// Create a camera entity
		auto camera = m_Scene->CreateEntity("Camera");
		auto& cameraTransform = camera.AddComponent<TransformComponent>();
		cameraTransform.Translation = { 0.0f, 5.0f, 10.0f };
		cameraTransform.Rotation = { 30.0f, 0.0f, 0.0f };

		auto& cameraComponent = camera.AddComponent<CameraComponent>();
		cameraComponent.Primary = true;
		cameraComponent.ProjectionMatrix = glm::perspective(
			glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 1000.0f);

		// Create a directional light
		auto light = m_Scene->CreateEntity("DirectionalLight");
		auto& lightTransform = light.AddComponent<TransformComponent>();
		lightTransform.Translation = { 5.0f, 10.0f, 5.0f };
		auto& lightComponent = light.AddComponent<LightComponent>();
		lightComponent.Type = LightComponent::Type::Directional;
		lightComponent.Direction = { 0.0f, -1.0f, 0.0f };
		lightComponent.Intensity = 1.0f;

		// Create a test cube entity
		auto cube = m_Scene->CreateEntity("TestCube");
		auto& cubeTransform = cube.AddComponent<TransformComponent>();
		cubeTransform.Translation = { 0.0f, 0.5f, 0.0f };
		cubeTransform.Scale = { 1.0f, 1.0f, 1.0f };

		// Create a test sphere entity
		auto sphere = m_Scene->CreateEntity("TestSphere");
		auto& sphereTransform = sphere.AddComponent<TransformComponent>();
		sphereTransform.Translation = { 3.0f, 0.5f, 0.0f };
		sphereTransform.Scale = { 0.5f, 0.5f, 0.5f };

		// Create a physics-enabled entity
		auto physicsEntity = m_Scene->CreateEntity("PhysicsBox");
		auto& physTransform = physicsEntity.AddComponent<TransformComponent>();
		physTransform.Translation = { -3.0f, 5.0f, 0.0f };
		physTransform.Scale = { 1.0f, 1.0f, 1.0f };
		auto& physRb = physicsEntity.AddComponent<RigidbodyComponent>();
		physRb.Type = RigidbodyComponent::BodyType::Dynamic;
		auto& physCollider = physicsEntity.AddComponent<BoxColliderComponent>();
		physCollider.Size = { 1.0f, 1.0f, 1.0f };

		// Create a script entity
		auto scriptEntity = m_Scene->CreateEntity("ScriptEntity");
		auto& scriptTransform = scriptEntity.AddComponent<TransformComponent>();
		scriptTransform.Translation = { 6.0f, 0.5f, 0.0f };
		scriptTransform.Scale = { 0.5f, 0.5f, 0.5f };
		auto& scriptComp = scriptEntity.AddComponent<ScriptComponent>();
		scriptComp.ClassName = "TestScript";
	}

	void EditorApp::Run()
	{
		// Create scene window
		CreateDefaultScene();

		// Main editor loop
		float lastFrameTime = 0.0f;
		while (IsRunning())
		{
			float time = (float)glfwGetTime();
			Timestep timestep = time - lastFrameTime;
			lastFrameTime = time;

			if (!IsMinimized())
			{
				// Update layers
				for (Layer* layer : GetLayerStack())
					layer->OnUpdate(timestep);

				// Update scene
				if (m_Scene)
				{
					if (m_IsPlaying)
					{
						if (m_IsPaused)
							m_Scene->OnUpdateEditor(timestep);
						else
							m_Scene->OnUpdateRuntime(timestep);
					}
					else
					{
						m_Scene->OnUpdateEditor(timestep);
					}
				}

				// Update camera
				if (m_Scene)
				{
					auto cameraEntity = m_Scene->GetPrimaryCameraEntity();
					if (cameraEntity)
					{
						auto& tc = cameraEntity.GetComponent<TransformComponent>();
						m_Camera.SetPosition(tc.Translation);
						m_Camera.SetRotation(tc.Rotation);
					}
				}

				// Render
				Renderer::BeginScene();

				if (m_Scene)
				{
					m_Scene->RenderScene();
				}

				Renderer::EndScene();
			}

			GetWindowScope()->OnUpdate();

			// Execute main thread queue
			ExecuteMainThreadQueue();

			// ImGui rendering
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			// Editor UI
			DrawEditorUI(timestep);

			ImGui::Render();

			// TODO: ImGui render to swapchain
		}
	}

	void EditorApp::DrawEditorUI(Timestep ts)
	{
		// Menu Bar
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("New Scene"))
				{
					m_Scene = CreateRef<Scene>();
				}
				if (ImGui::MenuItem("Open Scene"))
				{
					// TODO: File dialog
				}
				if (ImGui::MenuItem("Save Scene"))
				{
					if (m_Scene)
					{
						SceneSerializer serializer(m_Scene);
						serializer.Serialize("resources/scenes/editor.forge");
					}
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Exit"))
				{
					Close();
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Edit"))
			{
				if (ImGui::MenuItem("Undo")) { }
				if (ImGui::MenuItem("Redo")) { }
				ImGui::Separator();
				if (ImGui::MenuItem("Delete Selected"))
				{
					if (m_SelectedEntity)
					{
						m_Scene->DestroyEntity(m_SelectedEntity);
						m_SelectedEntity = {};
					}
				}
				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Scene"))
			{
				if (ImGui::MenuItem("Create Empty Entity"))
				{
					auto entity = m_Scene->CreateEntity("Entity");
					(void)entity;
				}
				if (ImGui::MenuItem("Create Cube"))
				{
					auto entity = m_Scene->CreateEntity("Cube");
					auto& tc = entity.AddComponent<TransformComponent>();
					tc.Translation = { 0.0f, 0.5f, 0.0f };
					tc.Scale = { 1.0f, 1.0f, 1.0f };
				}
				if (ImGui::MenuItem("Create Sphere"))
				{
					auto entity = m_Scene->CreateEntity("Sphere");
					auto& tc = entity.AddComponent<TransformComponent>();
					tc.Translation = { 0.0f, 0.5f, 0.0f };
					tc.Scale = { 0.5f, 0.5f, 0.5f };
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Play"))
				{
					m_IsPlaying = true;
					m_IsPaused = false;
					m_Scene->OnRuntimeStart();
				}
				if (ImGui::MenuItem("Pause"))
				{
					m_IsPaused = !m_IsPaused;
				}
				if (ImGui::MenuItem("Stop"))
				{
					m_IsPlaying = false;
					m_IsPaused = false;
					m_Scene->OnRuntimeStop();
				}
				ImGui::EndMenu();
			}

			ImGui::EndMainMenuBar();
		}

		// Scene Hierarchy Panel
		ImGui::Begin("Scene Hierarchy");
		if (m_Scene)
		{
			auto view = m_Scene->GetAllEntitiesWith<TagComponent>();
			for (auto entity : view)
			{
				Entity e = { entity, m_Scene.get() };
				bool selected = (e == m_SelectedEntity);

				ImGui::PushID((int)e);
				ImGui::Selectable(e.GetName().c_str(), selected);

				if (ImGui::IsItemClicked())
				{
					m_SelectedEntity = e;
				}

				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
				{
					// Rename
					ImGui::OpenPopup("Rename");
				}

				ImGui::PopID();
			}
		}
		ImGui::End();

		// Inspector Panel
		ImGui::Begin("Inspector");
		if (m_SelectedEntity)
		{
			ImGui::Text("Selected: %s", m_SelectedEntity.GetName().c_str());
			ImGui::Separator();

			// Tag
			ImGui::Text("Tag");
			ImGui::SameLine();
			char tagBuf[256];
			strncpy(tagBuf, m_SelectedEntity.GetName().c_str(), sizeof(tagBuf));
			tagBuf[sizeof(tagBuf) - 1] = '\0';
			ImGui::InputText("##Tag", tagBuf, sizeof(tagBuf));
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				auto& tag = m_SelectedEntity.GetComponent<TagComponent>();
				tag.Tag = tagBuf;
			}

			ImGui::Separator();

			// Transform
			ImGui::Text("Transform");
			ImGui::Separator();

			auto& tc = m_SelectedEntity.GetComponent<TransformComponent>();

			ImGui::Text("Translation");
			ImGui::DragFloat3("##Translation", (float*)&tc.Translation, 0.1f);

			ImGui::Text("Rotation");
			ImGui::DragFloat3("##Rotation", (float*)&tc.Rotation, 1.0f);

			ImGui::Text("Scale");
			ImGui::DragFloat3("##Scale", (float*)&tc.Scale, 0.01f, 0.01f);

			ImGui::Separator();

			// Components
			ImGui::Text("Components");
			ImGui::Separator();

			if (m_SelectedEntity.HasComponent<TransformComponent>())
			{
				ImGui::Text("  Transform (always present)");
			}
			if (m_SelectedEntity.HasComponent<RigidbodyComponent>())
			{
				ImGui::Text("  Rigidbody");
				ImGui::SameLine();
				ImGui::Checkbox("Fixed Rotation", &m_SelectedEntity.GetComponent<RigidbodyComponent>().FixedRotation);
			}
			if (m_SelectedEntity.HasComponent<BoxColliderComponent>())
			{
				ImGui::Text("  BoxCollider");
				ImGui::DragFloat3("Size", (float*)&m_SelectedEntity.GetComponent<BoxColliderComponent>().Size, 0.01f);
			}
			if (m_SelectedEntity.HasComponent<SphereColliderComponent>())
			{
				ImGui::Text("  SphereCollider");
				ImGui::DragFloat("Radius", &m_SelectedEntity.GetComponent<SphereColliderComponent>().Radius, 0.01f);
			}
			if (m_SelectedEntity.HasComponent<PBRMaterialComponent>())
			{
				ImGui::Text("  PBR Material");
				ImGui::ColorEdit3("Albedo", (float*)&m_SelectedEntity.GetComponent<PBRMaterialComponent>().Albedo);
				ImGui::SliderFloat("Metallic", &m_SelectedEntity.GetComponent<PBRMaterialComponent>().Metallic, 0.0f, 1.0f);
				ImGui::SliderFloat("Roughness", &m_SelectedEntity.GetComponent<PBRMaterialComponent>().Roughness, 0.0f, 1.0f);
			}
			if (m_SelectedEntity.HasComponent<LightComponent>())
			{
				ImGui::Text("  Light");
				ImGui::ColorEdit3("Color", (float*)&m_SelectedEntity.GetComponent<LightComponent>().Color);
				ImGui::SliderFloat("Intensity", &m_SelectedEntity.GetComponent<LightComponent>().Intensity, 0.0f, 10.0f);
			}

			ImGui::Separator();

			// Add Component
			ImGui::Text("Add Component");
			ImGui::Separator();

			if (ImGui::Button("Rigidbody"))
			{
				m_SelectedEntity.AddComponent<RigidbodyComponent>();
			}
			ImGui::SameLine();
			if (ImGui::Button("Box Collider"))
			{
				m_SelectedEntity.AddComponent<BoxColliderComponent>();
			}
			ImGui::SameLine();
			if (ImGui::Button("Sphere Collider"))
			{
				m_SelectedEntity.AddComponent<SphereColliderComponent>();
			}
			ImGui::SameLine();
			if (ImGui::Button("PBR Material"))
			{
				m_SelectedEntity.AddComponent<PBRMaterialComponent>();
			}
			ImGui::SameLine();
			if (ImGui::Button("Light"))
			{
				m_SelectedEntity.AddComponent<LightComponent>();
			}
			ImGui::SameLine();
			if (ImGui::Button("Script"))
			{
				ImGui::OpenPopup("AddScript");
			}

			// Remove Component
			ImGui::Separator();
			ImGui::Text("Remove Component");
			ImGui::Separator();

			if (m_SelectedEntity.HasComponent<RigidbodyComponent>())
			{
				if (ImGui::Button("Remove Rigidbody"))
				{
					m_SelectedEntity.RemoveComponent<RigidbodyComponent>();
				}
			}
			if (m_SelectedEntity.HasComponent<BoxColliderComponent>())
			{
				if (ImGui::Button("Remove BoxCollider"))
				{
					m_SelectedEntity.RemoveComponent<BoxColliderComponent>();
				}
			}
			if (m_SelectedEntity.HasComponent<SphereColliderComponent>())
			{
				if (ImGui::Button("Remove SphereCollider"))
				{
					m_SelectedEntity.RemoveComponent<SphereColliderComponent>();
				}
			}
			if (m_SelectedEntity.HasComponent<PBRMaterialComponent>())
			{
				if (ImGui::Button("Remove PBRMaterial"))
				{
					m_SelectedEntity.RemoveComponent<PBRMaterialComponent>();
				}
			}
			if (m_SelectedEntity.HasComponent<LightComponent>())
			{
				if (ImGui::Button("Remove Light"))
				{
					m_SelectedEntity.RemoveComponent<LightComponent>();
				}
			}
		}
		else
		{
			ImGui::Text("No entity selected");
		}
		ImGui::End();

		// Viewport Panel
		ImGui::Begin("Viewport");
		ImGui::Image(
			ImTextureRef{},
			ImVec2(800, 600)
		);
		ImGui::End();

		// Properties Panel
		ImGui::Begin("Properties");
		ImGui::Text("Editor Properties");
		ImGui::Separator();

		ImGui::Text("Time: %.2fs", ts.GetSeconds());
		ImGui::Text("FPS: %.1f", 1.0f / ts.GetSeconds());

		ImGui::Separator();
		ImGui::Text("Scene: %s", m_Scene ? m_Scene->GetEntityByUUID(m_SelectedEntity.GetUUID()).GetName().c_str() : "None");

		ImGui::Separator();
		ImGui::Text("Entities:");
		if (m_Scene)
		{
			int count = 0;
			auto view = m_Scene->GetAllEntitiesWith<TagComponent>();
			for (auto e : view) (void)e, count++;
			ImGui::Text("  Total: %d", count);
		}

		ImGui::End();
	}

	// Application factory
	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		(void)args;
		return new EditorApp();
	}

}
