#include "fgpch.h"
#include <cstdlib>
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
#include "Forge/Renderer/Camera.h"
#include "Forge/Renderer/RendererStats.h"

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"

#include <glm/gtc/matrix_inverse.hpp>

#include "Platform/Vulkan/VulkanVertexArray.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"
#include "Platform/Vulkan/VulkanSwapchain.h"
#include "Platform/Vulkan/VulkanTexture.h"
#include "Platform/Vulkan/PbrRenderer.h"
#include <algorithm>
#include <limits>
#include <vector>
#include "Forge/Renderer/CpuTexture.h"
#include "Forge/Assets/AssetWatcher.h"
#include "Forge/Debug/Instrumentor.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

namespace Forge {

	namespace {

		// One upload per model path, holding the CPU mesh it came from so the key
		// can never name freed memory (a raw-pointer key would be handed the next
		// mesh allocated at the same address and drawn under the wrong entity's
		// transform). Cleared in Renderer::Shutdown, with the other GPU resources.
		struct UploadedMesh
		{
			Ref<CpuMesh> Source;
			Ref<class Mesh> Upload;
		};

		std::unordered_map<std::string, UploadedMesh> s_UploadedMeshes;

		RendererStatistics s_Statistics;

		// The AABB of the shared fallback cube, so a meshless entity is culled
		// against the box it is actually drawn as.
		glm::vec3 DefaultCubeMin()
		{
			static const Ref<CpuMesh> cube = Primitives::CreateCube(1.0f);
			return cube->GetMin();
		}

		glm::vec3 DefaultCubeMax()
		{
			static const Ref<CpuMesh> cube = Primitives::CreateCube(1.0f);
			return cube->GetMax();
		}

		// The world-space AABB of a mesh's bounds drawn under `model`: the
		// bounding box is not axis-aligned after a rotation, so it is the AABB
		// of the transformed corners - a box culled against that is a box that
		// is *provably* off-screen for every pixel.
		void TransformAABB(const glm::vec3& min, const glm::vec3& max, const glm::mat4& model,
			glm::vec3& outMin, glm::vec3& outMax)
		{
			outMin = glm::vec3(std::numeric_limits<float>::max());
			outMax = glm::vec3(std::numeric_limits<float>::lowest());

			for (int i = 0; i < 8; ++i)
			{
				const glm::vec3 corner(
					(i & 1) ? max.x : min.x,
					(i & 2) ? max.y : min.y,
					(i & 4) ? max.z : min.z);
				const glm::vec3 world(model * glm::vec4(corner, 1.0f));
				outMin = glm::min(outMin, world);
				outMax = glm::max(outMax, world);
			}
		}

	}  // namespace

	Scope<Renderer::SceneData> Renderer::s_SceneData = CreateScope<Renderer::SceneData>();
	ShaderLibrary Renderer::s_ShaderLibrary;
	uint32_t Renderer::s_Width = 1920;
	uint32_t Renderer::s_Height = 1080;
	uint32_t Renderer::s_InitialWidth = 0;
	uint32_t Renderer::s_InitialHeight = 0;
	nvrhi::CommandListHandle Renderer::s_CommandList;
	Ref<VertexArray> Renderer::s_FullscreenQuad;
	Ref<Mesh> Renderer::s_DefaultCube;
	bool Renderer::s_SwapchainNeedsRecreate = false;
	void* Renderer::s_WindowHandle = nullptr;
	Ref<PbrRenderer> Renderer::s_PbrRenderer = CreateRef<PbrRenderer>();
	bool Renderer::s_RenderSceneVerified = false;
	nvrhi::FramebufferHandle Renderer::s_ShadowFramebuffer;
	nvrhi::FramebufferHandle Renderer::s_TonemappedFramebuffer;
	nvrhi::FramebufferHandle Renderer::s_SSAOFramebuffer;
	uint32_t Renderer::s_CurrentSwapchainImage = UINT32_MAX;
	float Renderer::s_RenderScale = 1.0f;

	void Renderer::Init()
	{
		RenderCommand::Init();

		// The swapchain and every offscreen target are sized from the render scale,
		// so set it before the surface is created.
		VulkanRendererAPI::SetRenderScale(s_RenderScale);

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

		// Bloom chain: luminance filter + 13-tap downsample, and the
		// 16-tap golden-spiral blur used for its iterations.
		auto bloomShader = Shader::CreateFromSPIRV("Bloom", "resources/shaders/fullscreen.vert.spv", "resources/shaders/bloom.frag.spv");
		if (bloomShader)
			s_ShaderLibrary.Add(bloomShader);

		auto blurShader = Shader::CreateFromSPIRV("BloomBlur", "resources/shaders/fullscreen.vert.spv", "resources/shaders/blur.frag.spv");
		if (blurShader)
			s_ShaderLibrary.Add(blurShader);

		auto shadowShader = Shader::CreateFromSPIRV("Shadow", "resources/shaders/shadow.vert.spv", "resources/shaders/shadow.frag.spv");
		if (shadowShader)
			s_ShaderLibrary.Add(shadowShader);

		// IBL shaders (irradiance + prefilter for environment map prefiltering).
		auto irradianceShader = Shader::CreateFromSPIRV("Irradiance", "resources/shaders/fullscreen.vert.spv", "resources/shaders/irradiance.frag.spv");
		if (irradianceShader)
			s_ShaderLibrary.Add(irradianceShader);

		auto prefilterShader = Shader::CreateFromSPIRV("Prefilter", "resources/shaders/fullscreen.vert.spv", "resources/shaders/prefilter.frag.spv");
		if (prefilterShader)
			s_ShaderLibrary.Add(prefilterShader);

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
			// Size the swapchain and every render target from the window rather
			// than a constant. The swapchain clamps itself to the surface extent,
			// so a guessed size left the offscreen targets a different size to the
			// swapchain and each post-processing pass set a viewport and scissor
			// larger than the attachment it drew into.
			if (s_InitialWidth > 0 && s_InitialHeight > 0)
			{
				s_Width = s_InitialWidth;
				s_Height = s_InitialHeight;
			}

			RenderCommand::GetRendererAPI()->SetupSwapchain(windowHandle, s_Width, s_Height);

			// Build the PBR pipeline for the format it actually draws into.
			auto& swapchain = VulkanRendererAPI::GetSwapchain();
			if (swapchain.IsValid())
			{
				// The swapchain may report a different extent than the one asked
				// for; the renderer tracks that as the frame size.
				s_Width = swapchain.GetWidth();
				s_Height = swapchain.GetHeight();

				// The PBR pass renders into the offscreen HDR target, not into the
				// swapchain, so the pipeline has to be built for the HDR target's
				// format. Under dynamic rendering a pipeline whose declared colour
				// format differs from the render pass attachment is a spec
				// violation, and drivers are entitled to drop the draw.
				nvrhi::Format hdrFormat = VulkanRendererAPI::GetHDRBuffer()
					? VulkanRendererAPI::GetHDRBuffer()->getDesc().format
					: swapchain.GetFormat();

				if (!s_PbrRenderer->Initialize(hdrFormat))
				{
					FG_CORE_ERROR("Renderer: the PBR pipeline could not be created; scene will not be drawn");
				}
				else
				{
					s_PbrRenderer->SetFrameSize(VulkanRendererAPI::GetRenderWidth(), VulkanRendererAPI::GetRenderHeight());
					s_PbrRenderer->Resize(VulkanRendererAPI::GetRenderWidth(), VulkanRendererAPI::GetRenderHeight());
				}

				// Create post-processing framebuffers using VulkanRendererAPI's accessors.
				s_TonemappedFramebuffer = VulkanRendererAPI::GetTonemappedFramebuffer();
				s_SSAOFramebuffer = VulkanRendererAPI::GetSSAOFramebuffer();

				// Set the HDR framebuffer for offscreen PBR rendering.
				s_PbrRenderer->SetHDRFramebuffer(
					VulkanRendererAPI::GetHDRFramebuffer(),
					VulkanRendererAPI::GetHDRBuffer(),
					s_PbrRenderer->GetDepthTexture()
				);
			}

			// Load IBL environment map if available.
			auto envMapPath = std::getenv("FG_ENV_MAP");
			if (envMapPath && std::string(envMapPath).size() > 0)
			{
				auto envTex = CpuTexture::Load(envMapPath);
				if (envTex)
				{
					// VulkanTexture needs raw pixel data; CpuTexture stores HDR as floats.
					nvrhi::Format format = envTex->IsHDR()
						? nvrhi::Format::RGBA16_FLOAT
						: nvrhi::Format::RGBA8_UNORM;
					auto gpuTex = CreateRef<VulkanTexture>(
						envTex->GetWidth(), envTex->GetHeight(),
						const_cast<void*>(envTex->GetPixelData()), format);
					if (gpuTex && gpuTex->GetTextureHandle() && s_PbrRenderer->LoadEnvironmentMap(gpuTex->GetTextureHandle()))
					{
						FG_CORE_INFO("IBL: environment map loaded from '{0}'", envMapPath);
					}
				}
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
		// Wait for GPU work to complete and retire command buffers before
		// releasing any resources.
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (device)
		{
			VulkanRendererAPI::WaitForGpuIdle();
			device->runGarbageCollection();
			device->runGarbageCollection(); // second pass
		}

		// Released here rather than at static destruction so the GPU buffers go
		// away while the Vulkan device is still alive.
		s_DefaultCube = nullptr;
		s_UploadedMeshes.clear();
	}

	bool Renderer::CaptureSwapchainImage(uint32_t imageIndex, const std::string& path)
	{
		if (imageIndex == UINT32_MAX)
			return false;

		return VulkanRendererAPI::GetSwapchain().CaptureImage(imageIndex, path);
	}

	void Renderer::RenderScene(Scene* scene, uint32_t swapchainImage)
	{
		FG_PROFILE_FUNCTION();
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

		// The primary camera, resolved the one way the engine resolves it. This
		// used to be a second, inline copy of that search - and it did not
		// require a TransformComponent, so the renderer could pick a camera that
		// the rest of the engine would not have picked for the same scene.
		Entity cameraEntity = scene->GetPrimaryCameraEntity();
		if (!cameraEntity)
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

		// Derive the view and projection from the camera transform.
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

		glm::vec3 lightDirection = { -0.5f, -1.0f, -0.3f };
		glm::vec3 lightColor = { 1.0f, 1.0f, 1.0f };
		float lightIntensity = 1.0f;

		if (hasDirectionalLight)
		{
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

		// Compute real shadow view-projection from the directional light.
		glm::mat4 shadowViewProjection = glm::mat4(1.0f);
		if (hasDirectionalLight)
		{
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

		// Shadow pass: render all meshes to the shadow map.
		BeginShadowPass(scene);

		// Get the HDR framebuffer (offscreen target).
		// The PBR renderer stores its HDR framebuffer after initialization.
		nvrhi::FramebufferHandle pbrFrameBuffer = VulkanRendererAPI::GetHDRFramebuffer();

		// Draw every entity carrying a mesh. Entities without one fall back to a
		// shared cube that is built once and kept for the lifetime of the
		// renderer: it owns GPU buffers, and a per-frame cube was being
		// destroyed while the GPU was still reading it.
		auto getMesh = [](const Ref<Mesh>& mesh) -> Ref<Mesh>
		{
			if (mesh)
				return mesh;

			if (!s_DefaultCube)
			{
				Ref<CpuMesh> cpuCube = Primitives::CreateCube(1.0f);
				s_DefaultCube = Mesh::CreateFromCpu(cpuCube.get(), Material{});
			}
			return s_DefaultCube;
		};

		auto meshEntities = scene->GetAllEntitiesWith<MeshComponent, TransformComponent>();
		uint32_t drawn = 0;
		s_Statistics.Reset();

		// The component holds the CPU geometry (the engine core imports it, so a
		// broken model path is reported where the entity was authored) and the
		// GPU mesh is derived from it here, because uploading needs a device.
		auto uploadMesh = [](MeshComponent& component) -> Ref<Mesh>
		{
			if (component.Mesh)
				return component.Mesh;

			if (!component.CpuMeshData)
				return nullptr;

			auto it = s_UploadedMeshes.find(component.ModelPath);
			if (it != s_UploadedMeshes.end() && it->second.Source == component.CpuMeshData)
			{
				component.Mesh = it->second.Upload;
				return component.Mesh;
			}

			// The material authored on the entity overrides the imported one; the
			// renderer's per-draw parameters are read from the PBR component below,
			// so the upload only carries geometry.
			Ref<Mesh> uploaded = Mesh::CreateFromCpu(component.CpuMeshData.get(), Material{});
			if (!uploaded)
				return nullptr;

			s_UploadedMeshes[component.ModelPath] = UploadedMesh{ component.CpuMeshData, uploaded };
			component.Mesh = uploaded;
			return component.Mesh;
		};

		struct DrawRecord
		{
			Ref<Mesh> MeshHandle;
			glm::mat4 Model;
			glm::mat3 NormalMatrix;
			glm::vec3 Albedo;
			glm::vec3 Emissive;
			float Metallic;
			float Roughness;
			float AO;
			float EmissiveIntensity;
			float Depth;
			Ref<Texture> AlbedoTexture;
		};

		std::vector<DrawRecord> records;

		// One frustum for the frame. Everything drawn this pass is drawn under
		// the camera that resolved above, so every entity is culled against the
		// same six planes the shader will light under.
		const FrustumCorners frustum = Frustum::CalculateCorners(camera.ViewProjectionMatrix);

		for (auto handle : meshEntities)
		{
			auto& meshComponent = meshEntities.get<MeshComponent>(handle);
			const auto& transform = meshEntities.get<TransformComponent>(handle);

			Ref<Mesh> mesh = getMesh(uploadMesh(meshComponent));
			if (!mesh)
				continue;

			glm::mat4 model = transform.GetTransform()
				* glm::scale(glm::mat4(1.0f), meshComponent.Scale);

			// Frustum culling: a mesh whose transformed bounding box is on the
			// outer side of one plane cannot cover a single pixel, and the draw
			// that would produce nothing costs a full pipeline draw.
			glm::vec3 boundsMin = meshComponent.CpuMeshData ? meshComponent.CpuMeshData->GetMin() : DefaultCubeMin();
			glm::vec3 boundsMax = meshComponent.CpuMeshData ? meshComponent.CpuMeshData->GetMax() : DefaultCubeMax();
			glm::vec3 worldMin, worldMax;
			TransformAABB(boundsMin, boundsMax, model, worldMin, worldMax);
			if (!Frustum::IntersectsAABB(frustum, worldMin, worldMax))
			{
				s_Statistics.AddFrustumCulled();
				continue;
			}

			glm::mat3 normalMatrix = glm::inverseTranspose(glm::mat3(model));

			glm::vec3 albedo = { 1.0f, 1.0f, 1.0f };
			float metallic = 0.0f;
			float roughness = 0.5f;
			float ambientOcclusion = 1.0f;
			glm::vec3 emissive = { 0.0f, 0.0f, 0.0f };
			float emissiveIntensity = 0.0f;
			Ref<Texture> albedoTexture;

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
				if (!material.AlbedoTexturePath.empty())
					albedoTexture = GetAlbedoTexture(material.AlbedoTexturePath);
			}

			const glm::vec3 objectPosition(model[3][0], model[3][1], model[3][2]);
			const glm::vec4 viewPosition = camera.ViewMatrix * glm::vec4(objectPosition, 1.0f);
			const float depth = viewPosition.z;

			records.push_back({ mesh, model, normalMatrix, albedo, emissive,
				metallic, roughness, ambientOcclusion, emissiveIntensity, depth,
				albedoTexture });
		}

		// Reflection probe pass: redraw the scene from the probe into a cubemap
		// *before* UpdateFrame records the real camera, because the probe writes
		// the shared camera buffer once per face and UpdateFrame overwrites it
		// with the main camera right after.
		if (s_PbrRenderer && s_PbrRenderer->IsReflectionProbeEnabled() && !records.empty())
		{
			std::vector<PbrRenderer::ProbeDraw> probeDraws;
			probeDraws.reserve(records.size());
			for (const DrawRecord& record : records)
			{
				probeDraws.push_back({
					record.MeshHandle, record.AlbedoTexture, record.Model, record.NormalMatrix,
					record.Albedo, record.Emissive,
					record.Metallic, record.Roughness, record.AO, record.EmissiveIntensity });
			}

			s_PbrRenderer->RenderReflectionProbe(s_CommandList.Get(),
				shadowViewProjection,
				lightDirection, lightColor, lightIntensity,
				glm::vec3(0.03f, 0.03f, 0.04f),
				probeDraws);
		}

		// Record the per-frame uniforms and clear the target.
		s_PbrRenderer->UpdateFrame(s_CommandList.Get(), pbrFrameBuffer,
			camera.ViewProjectionMatrix,
			shadowViewProjection,
			cameraTransform.Translation,
			lightDirection, lightColor, lightIntensity,
			glm::vec3(0.03f, 0.03f, 0.04f));

		std::sort(records.begin(), records.end(), [](const DrawRecord& a, const DrawRecord& b)
		{
			return a.Depth < b.Depth;
		});

		for (const DrawRecord& record : records)
		{
			s_PbrRenderer->DrawObject(s_CommandList.Get(), pbrFrameBuffer,
				record.MeshHandle, record.AlbedoTexture, record.Model, record.NormalMatrix,
				record.Albedo, record.Metallic, record.Roughness, record.AO,
				record.Emissive, record.EmissiveIntensity);
			s_Statistics.AddDrawCall(record.MeshHandle->GetIndexCount() / 3, record.AlbedoTexture != nullptr);
			++drawn;
		}


		// Store the inverse view-projection for SSAO.
		s_SceneData->ViewProjectionMatrix = camera.ViewProjectionMatrix;

		if (drawn > 0)
		{
			FG_CORE_INFO("RenderScene: presenting {0}x{1}, camera at ({2}, {3}, {4}), {5} mesh entities ({6} frustum-culled)",
				s_Width, s_Height,
				cameraTransform.Translation.x, cameraTransform.Translation.y, cameraTransform.Translation.z,
				drawn, s_Statistics.FrustumCulled);
		}
	}

	RendererStatistics Renderer::GetStatistics()
	{
		return s_Statistics;
	}

	bool Renderer::EnableReflectionProbe(uint32_t size)
	{
		if (!s_PbrRenderer || !s_PbrRenderer->IsValid())
		{
			FG_CORE_WARN("Renderer: reflection probe requested without a valid PBR renderer");
			return false;
		}
		return s_PbrRenderer->EnableReflectionProbe(size);
	}

	bool Renderer::IsReflectionProbeEnabled()
	{
		return s_PbrRenderer && s_PbrRenderer->IsReflectionProbeEnabled();
	}

	void Renderer::SetReflectionProbePosition(const glm::vec3& position)
	{
		if (s_PbrRenderer)
			s_PbrRenderer->SetReflectionProbePosition(position);
	}

	void Renderer::SetRenderScale(float scale)
	{
		s_RenderScale = scale > 1.0f ? std::min(scale, 4.0f) : 1.0f;

		VulkanRendererAPI::SetRenderScale(s_RenderScale);
		if (VulkanRendererAPI::GetSwapchain().IsValid())
			ApplyFrameSize(s_Width, s_Height);
	}

	float Renderer::GetRenderScale()
	{
		return s_RenderScale;
	}

	Ref<Texture> Renderer::GetAlbedoTexture(const std::string& path)
	{
		struct AlbedoCacheEntry
		{
			Ref<Texture> Albedo;
			FileSignature Signature;
		};
		static std::unordered_map<std::string, AlbedoCacheEntry> s_AlbedoTextureCache;

		// Hot-reload contract (the Hazel AssetManager pattern): the file is
		// re-stat-ed per path per frame - one stat - and a size/timestamp
		// change re-decodes and re-uploads it. A failed load is retried as
		// soon as the file appears, and a failed reload keeps the texture that
		// worked rather than swapping in a broken one.
		const FileSignature current = FileSignature::ForFile(path);

		auto cached = s_AlbedoTextureCache.find(path);
		if (cached != s_AlbedoTextureCache.end() && current == cached->second.Signature)
			return cached->second.Albedo;

		const bool hadEntry = cached != s_AlbedoTextureCache.end();
		const Ref<Texture> previous = hadEntry ? cached->second.Albedo : Ref<Texture>{};

		Ref<Texture> texture = Texture::Create(path);
		const bool loaded = texture && texture->GetWidth() > 0;

		if (!hadEntry && !loaded)
			FG_CORE_ERROR("Renderer: failed to load PBR albedo texture '{0}'", path);

		if (hadEntry && !loaded)
		{
			// Keep the texture that works; record the new signature so the
			// broken state is not re-reloaded every frame.
			FG_CORE_WARN("Renderer: reload of PBR albedo texture '{0}' failed; keeping previous texture", path);
			cached->second.Signature = current;
			return cached->second.Albedo;
		}

		if (hadEntry)
			FG_CORE_INFO("Renderer: reloaded PBR albedo texture '{0}'", path);

		if (previous)
		{
			// The old GPU texture dies with the last reference; its cached
			// PBR binding sets must go first, because the draw cache is keyed
			// by the raw handle and the replacement can land at the same
			// address.
			auto vulkanTexture = std::dynamic_pointer_cast<VulkanTexture>(previous);
			if (vulkanTexture && s_PbrRenderer)
				s_PbrRenderer->InvalidateAlbedoCache(vulkanTexture->GetTextureHandle().Get());
		}

		AlbedoCacheEntry entry;
		entry.Albedo = texture;
		entry.Signature = current;
		s_AlbedoTextureCache[path] = entry;
		return texture;
	}

	void Renderer::ApplyFrameSize(uint32_t width, uint32_t height)
	{
		s_Width = width;
		s_Height = height;

		if (auto* api = RenderCommand::GetRendererAPI())
			api->ResizeRenderTargets(width, height);

		// The PBR pass draws into the HDR target at its own size, and the
		// post-processing passes set a viewport from it. Both have to follow the
		// frame size, or the passes render into and read from targets that no
		// longer match the framebuffer they are given.
		const uint32_t renderWidth = VulkanRendererAPI::GetRenderWidth();
		const uint32_t renderHeight = VulkanRendererAPI::GetRenderHeight();
		if (s_PbrRenderer && s_PbrRenderer->IsValid())
		{
			s_PbrRenderer->SetFrameSize(renderWidth, renderHeight);
			s_PbrRenderer->Resize(renderWidth, renderHeight);
			s_PbrRenderer->SetHDRFramebuffer(
				VulkanRendererAPI::GetHDRFramebuffer(),
				VulkanRendererAPI::GetHDRBuffer(),
				s_PbrRenderer->GetDepthTexture());
		}

		// The post-processing framebuffers were rebuilt alongside the targets.
		s_TonemappedFramebuffer = VulkanRendererAPI::GetTonemappedFramebuffer();
		s_SSAOFramebuffer = VulkanRendererAPI::GetSSAOFramebuffer();
	}

	void Renderer::OnWindowResize(uint32_t width, uint32_t height)
	{
		RenderCommand::SetViewport(0, 0, width, height);

		ApplyFrameSize(width, height);

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
			{
				// The rebuilt swapchain clamps itself to the surface extent, so it
				// can come back at a different size than requested. The render
				// targets follow it, or every pass would set a viewport that does
				// not match the attachment it draws into.
				ApplyFrameSize(swapchain.GetWidth(), swapchain.GetHeight());

				imageIndex = swapchain.AcquireNextImage();
			}
		}

		// Store the current swapchain image index for the Final pass.
		s_CurrentSwapchainImage = imageIndex;

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

	void Renderer::BeginSSAOPass()
	{
		FG_PROFILE_FUNCTION();
		// SSAO pass: render fullscreen quad with SSAO shader.
		// Reads from the depth buffer, reconstructs normals, outputs SSAO occlusion.
		if (!s_CommandList)
			return;

		// Get the depth texture from the PBR renderer.
		nvrhi::TextureHandle depthTexture = s_PbrRenderer->GetDepthTexture();
		if (!depthTexture)
			return;

		// Pass the inverse view-projection matrix for depth reconstruction.
		VulkanRendererAPI::RenderSSAOPass(s_CommandList.Get(), depthTexture, s_SceneData->ViewProjectionMatrix);
	}

	void Renderer::EndSSAOPass()
	{
	}

	void Renderer::BeginBloomPass()
	{
		FG_PROFILE_FUNCTION();
		// Bloom pass: soft-threshold luminance + 13-tap downsample to half
		// res, then three golden-spiral blurs. Reads the HDR buffer (before
		// the tonemap) and leaves the blurred glow in the bloom target the
		// HDR pass binds.
		if (!s_CommandList)
			return;

		VulkanRendererAPI::RenderBloomPass(s_CommandList.Get());
	}

	void Renderer::EndBloomPass()
	{
	}

	void Renderer::SetBloomIntensity(float intensity)
	{
		VulkanRendererAPI::SetBloomIntensity(intensity);
	}

	float Renderer::GetBloomIntensity()
	{
		return VulkanRendererAPI::GetBloomIntensity();
	}

	void Renderer::BeginHDRPass()
	{
		FG_PROFILE_FUNCTION();
		// HDR pass: render fullscreen quad with HDR shader to tonemapped buffer.
		// Reads from the HDR buffer, applies ACES tonemapping, outputs to tonemapped buffer.
		if (!s_CommandList)
			return;

		VulkanRendererAPI::RenderHDRPass(s_CommandList.Get());
	}

	void Renderer::BeginFinalPass()
	{
		FG_PROFILE_FUNCTION();
		// Final pass: render fullscreen quad with Final shader to swapchain.
		// Reads from tonemapped buffer + SSAO buffer, composites to swapchain.
		if (!s_CommandList)
			return;

		nvrhi::FramebufferHandle finalFrameBuffer = VulkanRendererAPI::GetSwapchain().GetFramebuffer(s_CurrentSwapchainImage);
		if (!finalFrameBuffer)
		{
			FG_CORE_WARN("Final: no swapchain framebuffer available");
			return;
		}

		VulkanRendererAPI::RenderFinalPass(s_CommandList.Get(), finalFrameBuffer);
	}

	void Renderer::EndFinalPass()
	{
	}

}

#endif // FG_NO_VULKAN_RUNTIME