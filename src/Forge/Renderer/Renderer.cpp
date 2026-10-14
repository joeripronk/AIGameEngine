#include "fgpch.h"
#include "Forge/Renderer/Renderer.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Forge/Renderer/RenderCommand.h"
#include "Forge/Renderer/Shader.h"
#include "Forge/Renderer/ShaderLibrary.h"
#include "Forge/Renderer/VertexArray.h"
#include "Forge/Renderer/VertexBuffer.h"
#include "Forge/Renderer/IndexBuffer.h"
#include "Forge/Renderer/Texture.h"
#include "Forge/Renderer/Mesh.h"
#include "Forge/Renderer/CpuMesh.h"

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"

#include <glm/gtc/matrix_inverse.hpp>

#include "Platform/Vulkan/VulkanVertexArray.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"
#include "Platform/Vulkan/VulkanSwapchain.h"
#include "Platform/Vulkan/PbrRenderer.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

namespace Forge {

	Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
	ShaderLibrary Renderer::s_ShaderLibrary;
	uint32_t Renderer::s_Width = 1920;
	uint32_t Renderer::s_Height = 1080;
	nvrhi::CommandListHandle Renderer::s_CommandList;
	Ref<VertexArray> Renderer::s_FullscreenQuad;
	bool Renderer::s_SwapchainNeedsRecreate = false;
	void* Renderer::s_WindowHandle = nullptr;
	Ref<PbrRenderer> Renderer::s_PbrRenderer = CreateRef<PbrRenderer>();
	bool Renderer::s_RenderSceneVerified = false;
	nvrhi::FramebufferHandle Renderer::s_ShadowFramebuffer;
	nvrhi::FramebufferHandle Renderer::s_TonemappedFramebuffer;
	nvrhi::FramebufferHandle Renderer::s_SSAOFramebuffer;
	uint32_t Renderer::s_CurrentSwapchainImage = UINT32_MAX;

	void Renderer::Init()
	{
		RenderCommand::Init();

		// The backend owns the command list the frames are recorded into.
		s_CommandList = VulkanRendererAPI::GetStaticCommandList();

		// Load pre-compiled SPIR-V shaders from resources/shaders.
		auto pbrShader = Shader::CreateFromSPIRV("PBR", "resources/shaders/pbr.vert.spv", "resources/shaders/pbr.frag.spv");
		if (pbrShader)
			s_ShaderLibrary.Add(pbrShader);

		auto ssaoShader = Shader::CreateFromSPIRV("SSAO", "resources/shaders/fullscreen.vert.spv", "resources/shaders/ssao.frag.spv");
		if (ssaoShader)
			s_ShaderLibrary.Add(ssaoShader);

		auto hdrShader = Shader::CreateFromSPIRV("HDR", "resources/shaders/fullscreen.vert.spv", "resources/shaders/hdr.frag.spv");
		if (hdrShader)
			s_ShaderLibrary.Add(hdrShader);

		auto finalShader = Shader::CreateFromSPIRV("Final", "resources/shaders/fullscreen.vert.spv", "resources/shaders/final.frag.spv");
		if (finalShader)
			s_ShaderLibrary.Add(finalShader);

		auto shadowShader = Shader::CreateFromSPIRV("Shadow", "resources/shaders/shadow.vert.spv", "resources/shaders/shadow.frag.spv");
		if (shadowShader)
			s_ShaderLibrary.Add(shadowShader);

		// Create fullscreen quad
		auto quadVA = VertexArray::Create();

		float vertices[] = {
			-1.0f, -1.0f, 0.0f, 0.0f,
			 1.0f, -1.0f, 1.0f, 0.0f,
			 1.0f,  1.0f, 1.0f, 1.0f,
			-1.0f,  1.0f, 0.0f, 1.0f
		};

		uint32_t indices[] = { 0, 1, 2, 2, 3, 0 };

		auto vb = VertexBuffer::Create(sizeof(vertices), vertices);
		vb->SetLayout({ 2, 2 });
		quadVA->AddVertexBuffer(vb);
		quadVA->SetIndexBuffer(IndexBuffer::Create(6, indices));

		// Resolve the fullscreen quad's input layout against the post-process
		// shaders (they share fullscreen.vert: position + uv).
		if (auto vulkanQuad = std::dynamic_pointer_cast<VulkanVertexArray>(quadVA))
		{
			const std::vector<std::string> kFullscreenAttributes = { "aPosition", "aUV" };

			bool anyLayout = false;
			for (const char* shaderName : { "SSAO", "HDR", "Final" })
			{
				auto shader = s_ShaderLibrary.Get(shaderName);
				auto vulkanShader = std::dynamic_pointer_cast<VulkanShader>(shader);
				if (!vulkanShader)
					continue;
				anyLayout = vulkanQuad->UpdateLayout(vulkanShader.get(), kFullscreenAttributes) || anyLayout;
			}

			if (anyLayout)
			{
				FG_CORE_INFO("Fullscreen quad layout resolved (stride {0} bytes)", vulkanQuad->GetStride());
			}
			else
			{
				FG_CORE_WARN("Fullscreen quad layout could not be resolved; post-processing disabled");
			}
		}

		// Retain the quad so its GPU buffers outlive Init() and can be uploaded.
		s_FullscreenQuad = quadVA;

		// Create the Vulkan surface, presentation swapchain and the renderer's
		// size-dependent targets now that the window exists. Application sets the
		// native handle in its constructor, which runs before Init() in the
		// derived application.
		if (void* windowHandle = GetActiveWindowHandle())
		{
			RenderCommand::GetRendererAPI()->SetupSwapchain(windowHandle, s_Width, s_Height);

			// Build the PBR pipeline for the swapchain's colour format.
			auto& swapchain = VulkanRendererAPI::GetSwapchain();
			if (swapchain.IsValid())
			{
				s_Width = swapchain.GetWidth();
				s_Height = swapchain.GetHeight();

				if (!s_PbrRenderer->Initialize(swapchain.GetFormat()))
				{
					FG_CORE_ERROR("Renderer: the PBR pipeline could not be created; scene will not be drawn");
				}
				else
				{
					s_PbrRenderer->SetFrameSize(s_Width, s_Height);
					s_PbrRenderer->Resize(s_Width, s_Height);
				}

				// Create post-processing framebuffers using VulkanRendererAPI's accessors.
				s_TonemappedFramebuffer = VulkanRendererAPI::GetTonemappedFramebuffer();
				s_SSAOFramebuffer = VulkanRendererAPI::GetSSAOFramebuffer();
			}
		}
		else
		{
			FG_CORE_WARN("Renderer: no native window handle; presentation disabled");
		}

		FG_CORE_INFO("Renderer initialized with {0} shaders", s_ShaderLibrary.GetShaderCount());
	}

	void Renderer::Shutdown()
	{
	}

	bool Renderer::CaptureSwapchainImage(uint32_t imageIndex, const std::string& path)
	{
		if (imageIndex == UINT32_MAX)
			return false;

		return VulkanRendererAPI::GetSwapchain().CaptureImage(imageIndex, path);
	}

	void Renderer::RenderScene(Scene* scene, uint32_t swapchainImage)
	{
		if (!scene || swapchainImage == UINT32_MAX || !s_CommandList || !s_PbrRenderer->IsValid())
		{
			if (!s_RenderSceneVerified)
			{
				s_RenderSceneVerified = true;
				FG_CORE_WARN("RenderScene: skipped (scene={0}, image={1}, cmdList={2}, pbr={3})",
					(bool)scene, swapchainImage, (bool)s_CommandList, s_PbrRenderer->IsValid());
			}
			return;
		}

		auto framebuffer = VulkanRendererAPI::GetSwapchain().GetFramebuffer(swapchainImage);
		if (!framebuffer)
		{
			if (!s_RenderSceneVerified)
			{
				s_RenderSceneVerified = true;
				FG_CORE_WARN("RenderScene: no framebuffer for swapchain image {0}", swapchainImage);
			}
			return;
		}

		// Find the primary camera and the first directional light. Entity is a
		// value type, so keep copies rather than pointers into the view.
		Entity cameraEntity;
		bool hasCamera = false;

		auto cameras = scene->GetAllEntitiesWith<CameraComponent, TransformComponent>();
		for (auto handle : cameras)
		{
			if (cameras.get<CameraComponent>(handle).Primary)
			{
				cameraEntity = Entity(handle, scene);
				hasCamera = true;
				break;
			}
		}

		if (!hasCamera)
		{
			FG_CORE_WARN("RenderScene: no primary camera; nothing to draw");
			return;
		}

		auto lights = scene->GetAllEntitiesWith<LightComponent>();
		bool hasDirectionalLight = false;
		for (auto handle : lights)
		{
			if (lights.get<LightComponent>(handle).Type == LightComponent::Type::Directional)
			{
				hasDirectionalLight = true;
				break;
			}
		}

		auto& camera = cameraEntity.GetComponent<CameraComponent>();
		const auto& cameraTransform = cameraEntity.GetComponent<TransformComponent>();

		// Derive the view and projection from the camera transform. Rotation is
		// stored in radians (see TransformComponent::GetTransform), so it must
		// not be passed through glm::radians again.
		glm::mat4 cameraRotation = glm::toMat4(glm::quat(cameraTransform.Rotation));
		glm::mat4 cameraTransformMatrix =
			glm::translate(glm::mat4(1.0f), cameraTransform.Translation) * cameraRotation;

		camera.ViewMatrix = glm::inverse(cameraTransformMatrix);
		camera.ProjectionMatrix = glm::perspective(
			glm::radians(camera.FieldOfView),
			s_Height > 0 ? static_cast<float>(s_Width) / static_cast<float>(s_Height) : camera.AspectRatio,
			camera.NearClip,
			camera.FarClip);
		camera.ViewProjectionMatrix = camera.ProjectionMatrix * camera.ViewMatrix;

		// One-off confirmation that the scene is actually reaching the GPU.
		const bool firstFrame = !s_RenderSceneVerified;
		s_RenderSceneVerified = true;

		glm::vec3 lightDirection = { -0.5f, -1.0f, -0.3f };
		glm::vec3 lightColor = { 1.0f, 1.0f, 1.0f };
		float lightIntensity = 1.0f;

		if (hasDirectionalLight)
		{
			// Re-read the light that the loop above matched.
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
		}

		// Record the per-frame uniforms and clear the target.
		// Compute the real shadow view-projection from the directional light.
		glm::mat4 shadowViewProjection = glm::mat4(1.0f);
		if (hasDirectionalLight)
		{
			// Get light + transform together to avoid dangling reference.
			auto lightWithTransform = scene->GetAllEntitiesWith<LightComponent, TransformComponent>();
			for (auto handle : lightWithTransform)
			{
				const auto& light = lightWithTransform.get<LightComponent>(handle);
				if (light.Type != LightComponent::Type::Directional)
					continue;

				const auto& lightTransform = lightWithTransform.get<TransformComponent>(handle);
				glm::mat4 lightTransformMatrix =
					glm::translate(glm::mat4(1.0f), lightTransform.Translation)
					* glm::toMat4(glm::quat(lightTransform.Rotation));

				glm::mat4 lightView = glm::inverse(lightTransformMatrix);
				glm::mat4 lightProj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f);
				shadowViewProjection = lightProj * lightView;
				break;
			}
		}

		// Shadow pass: render all meshes to the shadow map from the light's perspective.
		BeginShadowPass(scene);

		s_PbrRenderer->UpdateFrame(s_CommandList.Get(), framebuffer,
			camera.ViewProjectionMatrix,
			shadowViewProjection,   // real shadow view-projection
			cameraTransform.Translation,
			lightDirection, lightColor, lightIntensity,
			glm::vec3(0.03f, 0.03f, 0.04f));

		// Draw every entity carrying a mesh.
		//
		// An entity with a MeshComponent but no uploaded mesh (for example one
		// that only names a model path) is drawn with the default cube, so
		// newly created entities are visible immediately.
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

		auto meshEntities = scene->GetAllEntitiesWith<MeshComponent, TransformComponent>();
		uint32_t drawn = 0;
		for (auto handle : meshEntities)
		{
			const auto& meshComponent = meshEntities.get<MeshComponent>(handle);
			const auto& transform = meshEntities.get<TransformComponent>(handle);

			Ref<Mesh> mesh = getMesh(meshComponent.Mesh);
			if (!mesh)
				continue;

			// The mesh's own scale multiplies the entity transform's scale.
			glm::mat4 model = transform.GetTransform()
				* glm::scale(glm::mat4(1.0f), meshComponent.Scale);

			// Normals use the inverse transpose so non-uniform scale stays correct.
			glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(model));

			glm::vec3 albedo = { 1.0f, 1.0f, 1.0f };
			float metallic = 0.0f;
			float roughness = 0.5f;
			float ambientOcclusion = 1.0f;
			glm::vec3 emissive = { 0.0f, 0.0f, 0.0f };
			float emissiveIntensity = 0.0f;

			Entity entity(handle, scene);
			if (entity.HasComponent<PBRMaterialComponent>())
			{
				const auto& material = entity.GetComponent<PBRMaterialComponent>();
				albedo = material.Albedo;
				metallic = material.Metallic;
				roughness = material.Roughness;
				ambientOcclusion = material.AO;
				emissive = material.Emissive;
				emissiveIntensity = material.EmissiveIntensity;
			}

			s_PbrRenderer->DrawObject(s_CommandList.Get(), framebuffer,
				mesh, model, normalMatrix,
				albedo, metallic, roughness, ambientOcclusion,
				emissive, emissiveIntensity);

			drawn++;
		}

		if (firstFrame)
		{
			FG_CORE_INFO("RenderScene: presenting {0}x{1}, camera at ({2}, {3}, {4}), {5} mesh entities",
				s_Width, s_Height,
				cameraTransform.Translation.x, cameraTransform.Translation.y, cameraTransform.Translation.z,
				drawn);
		}
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);

		s_Width = width;
		s_Height = height;

		// The swapchain is created once during Init(). Subsequent resizes mark it
		// for recreation, which AcquireSwapchainImage() performs lazily (on the
		// next frame, rather than mid-resize).
		if (VulkanRendererAPI::GetSwapchain().IsValid())
			s_SwapchainNeedsRecreate = true;
	}

	void Renderer::BeginScene()
	{
		if (!s_CommandList)
			return;

		// Open the frame's command list. nvrhi requires it to be open before any
		// resource writes (buffer uploads) or draw calls are recorded.
		s_CommandList->open();

		// Flush the static fullscreen quad's vertex/index data on the first
		// frame; subsequent frames have nothing pending.
		FlushPendingUploads(s_FullscreenQuad);
	}

	void Renderer::EndScene()
	{
		if (!s_CommandList)
			return;

		// Submit the recorded work to the graphics queue and close the list.
		s_CommandList->close();

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (device)
			device->executeCommandList(s_CommandList.Get());
	}

	void Renderer::FlushPendingUploads(const Ref<class VertexArray>& vertexArray)
	{
		if (!s_CommandList || !vertexArray)
			return;

		auto vulkanVertexArray = std::dynamic_pointer_cast<VulkanVertexArray>(vertexArray);
		if (vulkanVertexArray)
			vulkanVertexArray->UploadTo(s_CommandList.Get());
	}

	bool Renderer::HasSwapchain()
	{
		return VulkanRendererAPI::GetSwapchain().IsValid();
	}

	uint32_t Renderer::AcquireSwapchainImage()
	{
		auto& swapchain = VulkanRendererAPI::GetSwapchain();
		if (!swapchain.IsValid())
			return UINT32_MAX;

		uint32_t imageIndex = swapchain.AcquireNextImage();

		// An out-of-date swapchain (e.g. after a resize) is rebuilt so the next
		// frame has a usable presentation target.
		if (imageIndex == UINT32_MAX || s_SwapchainNeedsRecreate)
		{
			s_SwapchainNeedsRecreate = false;
			if (swapchain.Recreate(s_Width, s_Height))
				imageIndex = swapchain.AcquireNextImage();
		}

		return imageIndex;
	}

	void Renderer::PresentSwapchainImage(uint32_t imageIndex)
	{
		if (imageIndex == UINT32_MAX)
			return;

		auto& swapchain = VulkanRendererAPI::GetSwapchain();
		if (!swapchain.IsValid())
			return;

		if (!swapchain.Present(imageIndex))
		{
			// Out of date or suboptimal; the next AcquireSwapchainImage() rebuilds it.
			s_SwapchainNeedsRecreate = true;
		}
	}

	void Renderer::Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform)
	{
		if (!shader || !vertexArray)
			return;

		shader->Bind();
		shader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
		shader->SetMat4("u_Transform", transform);

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}

	void Renderer::Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform, const Ref<class Texture>& albedoTexture)
	{
		if (!shader || !vertexArray)
			return;

		(void)albedoTexture;
		shader->Bind();
		shader->SetMat4("u_ViewProjection", s_SceneData->ViewProjectionMatrix);
		shader->SetMat4("u_Transform", transform);
		shader->SetFloat3("u_Albedo", glm::vec3(1.0f));
		shader->SetFloat("u_Metallic", 0.0f);
		shader->SetFloat("u_Roughness", 0.5f);
		shader->SetFloat("u_AO", 1.0f);
		shader->SetFloat3("u_Emissive", glm::vec3(0.0f));
		shader->SetFloat("u_EmissiveIntensity", 0.0f);
		shader->SetFloat("u_LightIntensity", 1.0f);
		shader->SetFloat3("u_LightColor", glm::vec3(1.0f));
		shader->SetFloat3("u_LightDirection", glm::vec3(0.0f, -1.0f, 0.0f));
		shader->SetFloat3("u_AmbientLight", glm::vec3(0.02f, 0.02f, 0.02f));

		vertexArray->Bind();
		RenderCommand::DrawIndexed(vertexArray);
	}

	void Renderer::BeginShadowPass(class Scene* scene)
	{
		if (!s_CommandList || !s_PbrRenderer || !s_PbrRenderer->IsValid())
			return;

		if (!scene)
			return;

		// Find the primary directional light to compute the shadow view-projection.
		glm::mat4 shadowViewProjection = glm::mat4(1.0f);
		Entity lightEntity;

		auto lights = scene->GetAllEntitiesWith<LightComponent, TransformComponent>();
		for (auto handle : lights)
		{
			const auto& light = lights.get<LightComponent>(handle);
			if (light.Type == LightComponent::Type::Directional)
			{
				const auto& transform = lights.get<TransformComponent>(handle);
				glm::mat4 lightTransform = glm::translate(glm::mat4(1.0f), transform.Translation)
					* glm::toMat4(glm::quat(transform.Rotation));

				glm::mat4 lightView = glm::inverse(lightTransform);
				glm::mat4 lightProj = glm::ortho(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f);
				shadowViewProjection = lightProj * lightView;

				break;
			}
		}

		// Update the shadow camera buffer with the light's view-projection.
		auto shadowFb = s_PbrRenderer->GetShadowFramebuffer();
		if (shadowFb)
		{
			s_PbrRenderer->UpdateShadowFrame(s_CommandList.Get(), shadowFb, shadowViewProjection);
		}

		// Render all mesh entities to the shadow map.
		auto meshEntities = scene->GetAllEntitiesWith<MeshComponent, TransformComponent>();
		for (auto handle : meshEntities)
		{
			const auto& meshComponent = meshEntities.get<MeshComponent>(handle);
			const auto& transform = meshEntities.get<TransformComponent>(handle);

			Ref<Mesh> mesh = meshComponent.Mesh;
			if (!mesh)
				continue;

			glm::mat4 model = glm::translate(glm::mat4(1.0f), transform.Translation)
				* glm::toMat4(glm::quat(transform.Rotation))
				* glm::scale(glm::mat4(1.0f), transform.Scale);

			s_PbrRenderer->DrawShadowObject(s_CommandList.Get(), shadowFb, mesh, model);
		}
	}

	void Renderer::EndShadowPass()
	{
		// Shadow pass is complete; the shadow map is now ready for the PBR pass.
	}

	void Renderer::RenderShadowScene(class Scene* scene)
	{
		// Shadow pass is handled by BeginShadowPass / EndShadowPass pair.
		// This method is a convenience wrapper.
		BeginShadowPass(scene);
		EndShadowPass();
	}

	void Renderer::BeginSSAOPass()
	{
		// SSAO pass: render fullscreen quad with SSAO shader.
		// Reads from the depth buffer, reconstructs normals, outputs SSAO occlusion.
		if (!s_CommandList || !s_FullscreenQuad || !s_SSAOFramebuffer)
			return;

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return;

		auto ssaoShader = s_ShaderLibrary.Get("SSAO");
		if (!ssaoShader)
			return;

		auto vulkanShader = std::dynamic_pointer_cast<VulkanShader>(ssaoShader);
		if (!vulkanShader)
			return;

		auto vulkanQuad = std::dynamic_pointer_cast<VulkanVertexArray>(s_FullscreenQuad);
		if (!vulkanQuad)
			return;

		// Resolve the fullscreen quad's input layout against the SSAO shader.
		const std::vector<std::string> kFullscreenAttributes = { "aPosition", "aUV" };
		if (!vulkanQuad->UpdateLayout(vulkanShader.get(), kFullscreenAttributes))
			return;

		// Set the depth buffer as the input texture for SSAO.
		// The depth buffer is the swapchain's depth attachment.
		// For now, we'll use a placeholder - the real implementation would
		// pass the depth buffer to the SSAO shader.
		// TODO: Implement SSAO with depth reconstruction.
	}

	void Renderer::EndSSAOPass()
	{
	}

	void Renderer::BeginHDRPass()
	{
		// HDR pass: render fullscreen quad with HDR shader to tonemapped buffer.
		// Reads from the swapchain image (PBR output), applies ACES tonemapping.
		if (!s_CommandList || !s_FullscreenQuad || !s_TonemappedFramebuffer)
			return;

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return;

		auto hdrShader = s_ShaderLibrary.Get("HDR");
		if (!hdrShader)
			return;

		auto vulkanShader = std::dynamic_pointer_cast<VulkanShader>(hdrShader);
		if (!vulkanShader)
			return;

		auto vulkanQuad = std::dynamic_pointer_cast<VulkanVertexArray>(s_FullscreenQuad);
		if (!vulkanQuad)
			return;

		// Resolve the fullscreen quad's input layout against the HDR shader.
		const std::vector<std::string> kFullscreenAttributes = { "aPosition", "aUV" };
		if (!vulkanQuad->UpdateLayout(vulkanShader.get(), kFullscreenAttributes))
			return;

		// The HDR pass reads from the swapchain image (PBR output).
		// We need to bind the swapchain image as a texture.
		// The swapchain images are wrapped as nvrhi textures.
		// For now, we'll use a placeholder - the real implementation would
		// bind the current swapchain image as the input texture.
		// TODO: Implement HDR pass with swapchain image binding.
	}

	void Renderer::EndHDRPass()
	{
	}

	void Renderer::BeginFinalPass()
	{
		// Final pass: render fullscreen quad with Final shader to swapchain.
		// Reads from tonemapped buffer + SSAO buffer, composites to swapchain.
		if (!s_CommandList || !s_FullscreenQuad)
			return;

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return;

		auto finalShader = s_ShaderLibrary.Get("Final");
		if (!finalShader)
			return;

		auto vulkanShader = std::dynamic_pointer_cast<VulkanShader>(finalShader);
		if (!vulkanShader)
			return;

		auto vulkanQuad = std::dynamic_pointer_cast<VulkanVertexArray>(s_FullscreenQuad);
		if (!vulkanQuad)
			return;

		// Resolve the fullscreen quad's input layout against the Final shader.
		const std::vector<std::string> kFullscreenAttributes = { "aPosition", "aUV" };
		if (!vulkanQuad->UpdateLayout(vulkanShader.get(), kFullscreenAttributes))
			return;

		// The Final pass reads from the tonemapped buffer and SSAO buffer,
		// and outputs to the swapchain.
		// For now, we'll output directly to the swapchain framebuffer.
		// TODO: Implement Final pass with tonemapped + SSAO binding.
	}

	void Renderer::EndFinalPass()
	{
	}

	void Renderer::SubmitFinal()
	{
		// Final pass is handled by BeginFinalPass / EndFinalPass pair.
	}

}

#endif // FG_NO_VULKAN_RUNTIME
