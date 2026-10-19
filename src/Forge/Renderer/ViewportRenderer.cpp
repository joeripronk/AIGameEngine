#include "fgpch.h"
#include <glm/gtc/matrix_inverse.hpp>
#include "Forge/Renderer/ViewportRenderer.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Renderer/Camera.h"
#include "Forge/Renderer/Mesh.h"
#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Scene/Components.h"

#include "Platform/Vulkan/VkRenderer.h"

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
	}

	void ViewportRenderer::Shutdown()
	{
	}

	void ViewportRenderer::Resize(uint32_t width, uint32_t height)
	{
		m_Width = width;
		m_Height = height;
	}

	void ViewportRenderer::Render(Scene* scene, const glm::mat4& viewProjectionMatrix)
	{
		Vulkan::VkRenderer& renderer = Vulkan::VkRenderer::Get();
		if (!scene || !renderer.IsReady() || !renderer.HasTargets())
			return;

		// The viewport panel's camera is the editor camera (or, while
		// simulating, the scene camera the panel renders through); the scene's
		// primary camera supplies the transform the gizmos and audio share.
		auto cameraEntity = scene->GetPrimaryCameraEntity();
		if (!cameraEntity)
			return;

		auto& transform = cameraEntity.GetComponent<TransformComponent>();

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

		// BeginScene records the camera upload and re-clears the HDR target,
		// replacing whatever RenderScene drew under the scene camera. The
		// viewport has no shadow map of its own: the shadow binding is the
		// identity, so nothing is shadowed and the map from RenderScene is
		// never consulted for these draws.
		Vulkan::VkRenderer::CameraParams cameraParams{};
		cameraParams.ViewProjection = viewProjectionMatrix;
		cameraParams.ShadowViewProjection = glm::mat4(1.0f);
		cameraParams.CameraPosition = transform.Translation;
		cameraParams.LightDirection = lightDirection;
		cameraParams.LightColor = lightColor;
		cameraParams.LightIntensity = lightIntensity;
		cameraParams.AmbientLight = { 0.03f, 0.03f, 0.04f };
		renderer.BeginScene(cameraParams);

		auto drawEntity = [&](const Ref<Mesh>& mesh, const Ref<Texture>& albedoTexture,
			const glm::mat4& model, const glm::vec3& albedo, float metallic, float roughness,
			float ao, const glm::vec3& emissive, float emissiveIntensity)
		{
			Vulkan::VkRenderer::ObjectParams object{};
			object.Model = model;
			object.NormalMatrix = glm::inverseTranspose(glm::mat3(model));
			object.Albedo = albedo;
			object.Metallic = metallic;
			object.Roughness = roughness;
			object.AO = ao;
			object.Emissive = emissive;
			object.EmissiveIntensity = emissiveIntensity;
			renderer.DrawObject(mesh, albedoTexture, object);
		};

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

			drawEntity(mesh, albedoTexture, model, albedo, metallic, roughness, ao, emissive, emissiveIntensity);
		}

		// Render entities with PBR material but no mesh (use default cube)
		auto materialView = scene->GetAllEntitiesWith<TransformComponent, PBRMaterialComponent>();
		for (auto entity : materialView)
		{
			// Skip entities that already have a MeshComponent (they were drawn above).
			if (meshView.contains(entity))
				continue;

			auto& transformComp = materialView.get<TransformComponent>(entity);
			auto& pbrComp = materialView.get<PBRMaterialComponent>(entity);
			Ref<Texture> albedoTexture;
			if (!pbrComp.AlbedoTexturePath.empty())
				albedoTexture = Renderer::GetAlbedoTexture(pbrComp.AlbedoTexturePath);

			Ref<Mesh> mesh = getMesh(nullptr);

			glm::mat4 model = transformComp.GetTransform()
				* glm::scale(glm::mat4(1.0f), pbrComp.Albedo);  // use albedo as scale proxy

			drawEntity(mesh, albedoTexture, model, pbrComp.Albedo, pbrComp.Metallic, pbrComp.Roughness,
				pbrComp.AO, pbrComp.Emissive, pbrComp.EmissiveIntensity);
		}
	}

}
