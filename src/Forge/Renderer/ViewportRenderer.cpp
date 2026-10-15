#include "fgpch.h"
#include "Forge/Renderer/ViewportRenderer.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Renderer/Shader.h"
#include "Forge/Renderer/ShaderLibrary.h"
#include "Forge/Renderer/VertexArray.h"
#include "Forge/Renderer/VertexBuffer.h"
#include "Forge/Renderer/IndexBuffer.h"
#include "Forge/Renderer/Camera.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

namespace Forge {

	ViewportRenderer::ViewportRenderer()
		: m_Width(800), m_Height(600)
	{
	}

	ViewportRenderer::~ViewportRenderer()
	{
	}

	void ViewportRenderer::Init(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;
		CreateRenderTargets();
	}

	void ViewportRenderer::Shutdown()
	{
		DestroyRenderTargets();
	}

	void ViewportRenderer::Resize(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;
		DestroyRenderTargets();
		CreateRenderTargets();
	}

	void ViewportRenderer::Render(Scene* scene, const glm::mat4& viewProjectionMatrix)
	{
		(void)viewProjectionMatrix;

		if (!scene)
			return;

		// Get primary camera
		auto cameraEntity = scene->GetPrimaryCameraEntity();
		if (!cameraEntity)
			return;

		auto& camera = cameraEntity.GetComponent<CameraComponent>();
		auto& transform = cameraEntity.GetComponent<TransformComponent>();

		// Update camera matrices
		glm::mat4 rotation = glm::toMat4(glm::quat(glm::radians(transform.Rotation)));
		glm::mat4 translation = glm::translate(glm::mat4(1.0f), transform.Translation);
		camera.ViewMatrix = glm::inverse(translation * rotation);
		camera.ViewProjectionMatrix = camera.ProjectionMatrix * camera.ViewMatrix;

		// Note: We don't call BeginScene/EndScene here because the viewport is rendered
		// within the main render pass (EditorApp::Run) which already has an open command buffer.

		// Set view projection matrix in shader
		auto pbrShader = Renderer::GetShaderLibrary().Get("PBR");
		if (pbrShader)
		{
			pbrShader->SetMat4("u_ViewProjection", camera.ViewProjectionMatrix);
		}

		// Render all mesh entities
		auto meshView = scene->GetAllEntitiesWith<TransformComponent, MeshComponent>();
		for (auto entity : meshView)
		{
			auto& transformComp = meshView.get<TransformComponent>(entity);
			// TODO: Render mesh from meshComp.Mesh
			(void)transformComp;
		}

		// Render entities with PBR material but no mesh (use default cube)
		auto materialView = scene->GetAllEntitiesWith<TransformComponent, PBRMaterialComponent>();
		for (auto entity : materialView)
		{
			auto& transformComp = materialView.get<TransformComponent>(entity);
			// TODO: Render default cube with PBR material
			(void)transformComp;
		}
	}

	void ViewportRenderer::CreateRenderTargets()
	{
		// Create render target texture
		m_RenderTexture = Texture::Create(m_Width, m_Height, nullptr);
	}

	void ViewportRenderer::DestroyRenderTargets()
	{
		m_RenderTexture = nullptr;
	}

}
