#include "fgpch.h"
#include <glm/gtc/matrix_inverse.hpp>
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
#include "Forge/Renderer/Mesh.h"
#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Scene/Components.h"

#include "Platform/Vulkan/VulkanRendererAPI.h"
#include "Platform/Vulkan/PbrRenderer.h"

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

		// Set view projection matrix in shader
		auto pbrShader = Renderer::GetShaderLibrary().Get("PBR");
		if (pbrShader)
		{
			pbrShader->SetMat4("u_ViewProjection", camera.ViewProjectionMatrix);
		}

		// Draw into the HDR framebuffer using the PBR renderer.
		Ref<Mesh> defaultCube;
		auto getMesh = [&defaultCube](const Ref<Mesh>& mesh) -> Ref<Mesh>
		{
			if (mesh)
				return mesh;

			if (!defaultCube)
			{
				Ref<CpuMesh> cpuCube = Primitives::CreateCube(1.0f);
				defaultCube = Mesh::CreateFromCpu(cpuCube.get(), Material{});
			}
			return defaultCube;
		};

		auto pbr = Renderer::GetPbrRenderer();
		if (!pbr || !pbr->IsValid())
			return;

		nvrhi::CommandListHandle cmdList = VulkanRendererAPI::GetStaticCommandList();
		nvrhi::FramebufferHandle framebuffer = VulkanRendererAPI::GetHDRFramebuffer();
		if (!cmdList || !framebuffer)
			return;

		// Light parameters (same as Renderer::RenderScene).
		glm::vec3 lightDirection = { -0.5f, -1.0f, -0.3f };
		glm::vec3 lightColor = { 1.0f, 1.0f, 1.0f };
		float lightIntensity = 1.0f;

		auto lights = scene->GetAllEntitiesWith<LightComponent>();
		for (auto handle : lights)
		{
			const auto& light = lights.get<LightComponent>(handle);
			if (light.Type != LightComponent::Type::Directional)
				continue;

			lightDirection = glm::normalize(light.Direction);
			lightColor = light.Color;
			lightIntensity = light.Intensity;
			break;
		}

		// Call UpdateFrame once to set up the camera uniform and clear the target.
		pbr->UpdateFrame(cmdList.Get(), framebuffer,
			viewProjectionMatrix,
			glm::mat4(1.0f),  // shadowViewProjection (identity for viewport)
			transform.Translation,
			lightDirection, lightColor, lightIntensity,
			glm::vec3(0.03f, 0.03f, 0.04f));

		// Render all mesh entities
		auto meshView = scene->GetAllEntitiesWith<TransformComponent, MeshComponent>();
		for (auto entity : meshView)
		{
			auto& meshComp = meshView.get<MeshComponent>(entity);
			auto& transformComp = meshView.get<TransformComponent>(entity);

			Ref<Mesh> mesh = getMesh(meshComp.Mesh);
			if (!mesh)
				continue;

			glm::mat4 model = transformComp.GetTransform()
				* glm::scale(glm::mat4(1.0f), meshComp.Scale);

			glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(model));

			glm::vec3 albedo = { 1.0f, 1.0f, 1.0f };
			float metallic = 0.0f;
			float roughness = 0.5f;
			float ao = 1.0f;
			glm::vec3 emissive = { 0.0f, 0.0f, 0.0f };
			float emissiveIntensity = 0.0f;
			Ref<Texture> albedoTexture;

			Entity ent(entity, scene);
			if (ent.HasComponent<PBRMaterialComponent>())
			{
				const auto& material = ent.GetComponent<PBRMaterialComponent>();
				albedo = material.Albedo;
				metallic = material.Metallic;
				roughness = material.Roughness;
				ao = material.AO;
				emissive = material.Emissive;
				emissiveIntensity = material.EmissiveIntensity;
				if (!material.AlbedoTexturePath.empty())
					albedoTexture = Renderer::GetAlbedoTexture(material.AlbedoTexturePath);
			}

			pbr->DrawObject(cmdList.Get(), framebuffer,
				mesh, albedoTexture, model, normalMatrix,
				albedo, metallic, roughness, ao,
				emissive, emissiveIntensity);
		}

		// Render entities with PBR material but no mesh (use default cube)
		auto materialView = scene->GetAllEntitiesWith<TransformComponent, PBRMaterialComponent>();
		for (auto entity : materialView)
		{
			// Skip entities that already have a MeshComponent (they were drawn above).
			if (materialView.contains(entity))
				continue;

			auto& transformComp = materialView.get<TransformComponent>(entity);
			auto& pbrComp = materialView.get<PBRMaterialComponent>(entity);
			Ref<Texture> albedoTexture;
			if (!pbrComp.AlbedoTexturePath.empty())
				albedoTexture = Renderer::GetAlbedoTexture(pbrComp.AlbedoTexturePath);
			}

			Ref<Mesh> mesh = getMesh(nullptr);

			glm::mat4 model = transformComp.GetTransform()
				* glm::scale(glm::mat4(1.0f), pbrComp.Albedo);  // use albedo as scale proxy

			glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(model));

			pbr->DrawObject(cmdList.Get(), framebuffer,
				mesh, albedoTexture, model, normalMatrix,
				pbrComp.Albedo, pbrComp.Metallic, pbrComp.Roughness,
				pbrComp.AO, pbrComp.Emissive, pbrComp.EmissiveIntensity);
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
