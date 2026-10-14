#include "fgpch.h"
#include "Platform/Vulkan/PbrRenderer.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"
#include "Platform/Vulkan/VulkanSwapchain.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanVertexArray.h"
#include "Platform/Vulkan/VulkanVertexBuffer.h"
#include "Platform/Vulkan/VulkanIndexBuffer.h"

#include "Forge/Renderer/Mesh.h"
#include "Forge/Renderer/VertexArray.h"
#include "Forge/Renderer/ShaderLibrary.h"
#include "Forge/Renderer/Renderer.h"

#include <cstring>

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	namespace {

		// nvrhi rounds constant buffer sizes up to this alignment.
		constexpr uint64_t kConstantBufferAlignment = 256;

		// A single opaque texel, used wherever a material has no map bound.
		constexpr uint8_t kWhitePixel[4] = { 255, 255, 255, 255 };

		// The vertex attributes declared by pbr.vert, in layout order.
		const std::vector<std::string> kPbrAttributes = {
			"aPosition", "aNormal", "aUV", "aTangent", "aBitangent"
		};

		// nvrhi::BindingSetItem has no aggregate-friendly constructor, so build
		// the items explicitly.
		nvrhi::BindingSetItem MakeConstantBufferItem(nvrhi::IBuffer* buffer, uint32_t slot)
		{
			nvrhi::BindingSetItem item{};
			item.resourceHandle = buffer;
			item.slot = slot;
			item.type = nvrhi::ResourceType::ConstantBuffer;
			item.range = nvrhi::BufferRange(0, 0);   // whole buffer
			return item;
		}

		nvrhi::BindingSetItem MakeTextureItem(nvrhi::ITexture* texture, uint32_t slot)
		{
			nvrhi::BindingSetItem item{};
			item.resourceHandle = texture;
			item.slot = slot;
			item.type = nvrhi::ResourceType::Texture_SRV;
			item.dimension = nvrhi::TextureDimension::Texture2D;
			// The SRV must name a concrete view format; an UNKNOWN value makes
			// nvrhi's Vulkan backend assert in convertFormat().
			item.format = texture->getDesc().format;
			item.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);   // base mip, 1 level, base slice, 1 slice
			return item;
		}

		nvrhi::BindingSetItem MakeSamplerItem(nvrhi::ISampler* sampler, uint32_t slot)
		{
			nvrhi::BindingSetItem item{};
			item.resourceHandle = sampler;
			item.slot = slot;
			item.type = nvrhi::ResourceType::Sampler;
			return item;
		}

	}

	bool PbrRenderer::Initialize(nvrhi::Format colorFormat)
	{
		(void)colorFormat;
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
		{
			FG_CORE_ERROR("PbrRenderer: renderer device not initialized");
			return false;
		}

		m_ColorFormat = colorFormat;

		if (!CreateBindingLayouts())		return false;
		if (!CreateSharedResources())		return false;
		if (!CreatePipelines(colorFormat)) return false;

		// Shadow map setup (2048x2048 depth texture).
		if (!CreateShadowMap(2048, 2048))	return false;
		if (!CreateShadowBindingLayout())	return false;
		if (!CreateShadowResources())		return false;
		if (!CreateShadowPipelines())		return false;

		// IBL setup (deferred until environment map is loaded).
		if (!CreateIBLFullscreenQuad())		return false;
		if (!CreateIBLShaders())			return false;
		if (!CreateIBLBindingLayout())		return false;
		if (!CreateIBLPipelines())			return false;

		FG_CORE_INFO("PBR renderer initialized (colour format {0}, shadow map 2048x2048)", (int)colorFormat);
		return true;
	}

	bool PbrRenderer::CreateBindingLayouts()
	{
		auto device = VulkanRendererAPI::GetStaticDevice();

		// Set 0: camera block, object block, albedo (combined), shadow (combined),
		// linear sampler (unused in PBR but kept for compatibility), shadow params,
		// env map (combined), irradiance map (combined).
		nvrhi::BindingLayoutDesc frameLayoutDesc;
		frameLayoutDesc.visibility = nvrhi::ShaderType::All;
		frameLayoutDesc.bindings = {
			{ 0, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
			{ 1, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
			{ 2, nvrhi::ResourceType::Texture_SRV, 0, 1 },
			{ 3, nvrhi::ResourceType::Texture_SRV, 0, 1 },
			{ 4, nvrhi::ResourceType::Sampler, 0, 1 },
			{ 5, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
			{ 6, nvrhi::ResourceType::Texture_SRV, 0, 1 },
			{ 7, nvrhi::ResourceType::Texture_SRV, 0, 1 },
		};
		m_FrameLayout = device->createBindingLayout(frameLayoutDesc);
		if (!m_FrameLayout)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create the frame binding layout");
			return false;
		}

		return true;
	}

	bool PbrRenderer::CreateSharedResources()
	{
		auto device = VulkanRendererAPI::GetStaticDevice();

		nvrhi::BufferDesc cameraDesc;
		cameraDesc.byteSize = kConstantBufferAlignment;
		cameraDesc.isConstantBuffer = true;
		cameraDesc.debugName = "PbrRenderer.Camera";
		cameraDesc.keepInitialState = true;
		cameraDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		m_CameraBuffer = device->createBuffer(cameraDesc);

		if (!m_CameraBuffer)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create the camera uniform buffer");
			return false;
		}

		// Linear, clamped sampler for the material and shadow maps.
		nvrhi::SamplerDesc samplerDesc;
		samplerDesc.addressU = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.addressV = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.addressW = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.setMinFilter(true);
		samplerDesc.setMagFilter(true);
		samplerDesc.setMipFilter(true);
		samplerDesc.setMaxAnisotropy(1.0f);
		samplerDesc.setBorderColor(nvrhi::Color(1.0f, 1.0f, 1.0f, 1.0f));
		m_LinearSampler = device->createSampler(samplerDesc);

		// 1x1 white fallback: sampling it multiplies the albedo by 1.
		nvrhi::TextureDesc whiteDesc;
		whiteDesc.width = 1;
		whiteDesc.height = 1;
		whiteDesc.format = nvrhi::Format::RGBA8_UNORM;
		whiteDesc.isShaderResource = true;
		whiteDesc.debugName = "PbrRenderer.White";
		whiteDesc.keepInitialState = true;
		whiteDesc.initialState = nvrhi::ResourceStates::ShaderResource;
		m_WhiteTexture = device->createTexture(whiteDesc);

		// A white "fully lit" shadow map, used until the shadow pass renders one.
		nvrhi::TextureDesc shadowDesc;
		shadowDesc.width = 1;
		shadowDesc.height = 1;
		shadowDesc.format = nvrhi::Format::RGBA8_UNORM;
		shadowDesc.isShaderResource = true;
		shadowDesc.debugName = "PbrRenderer.ShadowFallback";
		shadowDesc.keepInitialState = true;
		shadowDesc.initialState = nvrhi::ResourceStates::ShaderResource;
		m_ShadowTexture = device->createTexture(shadowDesc);

		// Fill the 1x1 textures. nvrhi records texture writes into a command list.
		nvrhi::CommandListHandle uploadList = device->createCommandList();
		uploadList->open();
		uploadList->writeTexture(m_WhiteTexture, 0, 0, kWhitePixel, sizeof(kWhitePixel), sizeof(kWhitePixel));
		uploadList->writeTexture(m_ShadowTexture, 0, 0, kWhitePixel, sizeof(kWhitePixel), sizeof(kWhitePixel));
		uploadList->close();
		device->executeCommandList(uploadList.Get());
		device->waitForIdle();

		// 1x1 white fallback for the environment map.
		nvrhi::TextureDesc envFallbackDesc;
		envFallbackDesc.width = 1;
		envFallbackDesc.height = 1;
		envFallbackDesc.format = nvrhi::Format::RGBA16_FLOAT;
		envFallbackDesc.isShaderResource = true;
		envFallbackDesc.debugName = "PbrRenderer.EnvFallback";
		envFallbackDesc.keepInitialState = true;
		envFallbackDesc.initialState = nvrhi::ResourceStates::ShaderResource;
		m_EnvMapFallback = device->createTexture(envFallbackDesc);

		// ShadowMapParams uniform buffer.
		nvrhi::BufferDesc shadowParamsDesc;
		shadowParamsDesc.byteSize = 32;
		shadowParamsDesc.isConstantBuffer = true;
		shadowParamsDesc.debugName = "PbrRenderer.ShadowParams";
		shadowParamsDesc.keepInitialState = true;
		shadowParamsDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		m_ShadowParamsBuffer = device->createBuffer(shadowParamsDesc);

		return true;
	}

	bool PbrRenderer::CreatePipelines(nvrhi::Format colorFormat)
	{
		auto device = VulkanRendererAPI::GetStaticDevice();

		// The pipeline needs the PBR shader modules.
		nvrhi::ShaderHandle vertexShader, fragmentShader;
		if (auto shader = std::dynamic_pointer_cast<VulkanShader>(
				Renderer::GetShaderLibrary().Get("PBR")))
		{
			vertexShader = shader->GetVertexShader();
			fragmentShader = shader->GetFragmentShader();
		}

		if (!vertexShader || !fragmentShader)
		{
			FG_CORE_ERROR("PbrRenderer: the PBR shader is not available");
			return false;
		}

		m_VertexShader = vertexShader;
		m_FragmentShader = fragmentShader;

		return true;
	}

	nvrhi::IGraphicsPipeline* PbrRenderer::GetPipelineForLayout(nvrhi::IInputLayout* inputLayout)
	{
		if (!inputLayout || !m_VertexShader || !m_FragmentShader)
			return nullptr;

		// A pipeline bakes in its vertex layout, so meshes with different
		// attribute sets get their own pipeline. Cache them by layout.
		auto cached = m_PipelineCache.find(inputLayout);
		if (cached != m_PipelineCache.end())
			return cached->second.Get();

		auto device = VulkanRendererAPI::GetStaticDevice();

		nvrhi::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.VS = m_VertexShader;
		pipelineDesc.PS = m_FragmentShader;
		pipelineDesc.inputLayout = inputLayout;
		pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;

		pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
		pipelineDesc.renderState.depthStencilState.depthWriteEnable = true;
		pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;

		pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		pipelineDesc.renderState.rasterState.frontCounterClockwise = true;

		pipelineDesc.bindingLayouts.push_back(m_FrameLayout);

		nvrhi::FramebufferInfo framebufferInfo;
		framebufferInfo.colorFormats.push_back(m_ColorFormat);
		framebufferInfo.depthFormat = nvrhi::Format::D32;
		framebufferInfo.sampleCount = 1;

		nvrhi::GraphicsPipelineHandle pipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
		if (!pipeline)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create a graphics pipeline for a vertex layout");
			return nullptr;
		}

		m_PipelineCache.emplace(inputLayout, pipeline);
		return pipeline.Get();
	}

	bool PbrRenderer::Resize(uint32_t width, uint32_t height)
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device || width == 0 || height == 0)
			return false;

		if (m_DepthTexture && m_DepthWidth == width && m_DepthHeight == height)
			return true;

		nvrhi::TextureDesc depthDesc;
		depthDesc.width = width;
		depthDesc.height = height;
		depthDesc.format = nvrhi::Format::D32;
		depthDesc.isRenderTarget = true;
		depthDesc.debugName = "PbrRenderer.Depth";
		depthDesc.keepInitialState = true;
		depthDesc.initialState = nvrhi::ResourceStates::RenderTarget;

		m_DepthTexture = device->createTexture(depthDesc);
		m_DepthWidth = width;
		m_DepthHeight = height;

		if (!m_DepthTexture)
			return false;

		// The swapchain's framebuffers must carry the same depth attachment as
		// the pipeline's framebuffer format.
		VulkanRendererAPI::GetSwapchain().SetDepthTexture(m_DepthTexture);

		return true;
	}

	void PbrRenderer::UpdateFrame(nvrhi::ICommandList* commandList,
		nvrhi::FramebufferHandle framebuffer,
		const glm::mat4& viewProjection,
		const glm::mat4& shadowViewProjection,
		const glm::vec3& cameraPosition,
		const glm::vec3& lightDirection,
		const glm::vec3& lightColor,
		float lightIntensity,
		const glm::vec3& ambientLight)
	{
		if (!commandList || !framebuffer)
			return;

		// Record the camera uniform for this frame.
		CameraUniforms uniforms{};
		uniforms.ViewProjection = viewProjection;
		uniforms.ShadowViewProjection = shadowViewProjection;
		uniforms.LightDirection = glm::vec4(lightDirection, 0.0f);
		uniforms.LightColor = glm::vec4(lightColor, lightIntensity);
		uniforms.CameraPosition = glm::vec4(cameraPosition, 0.0f);
		uniforms.AmbientLight = glm::vec4(ambientLight, 0.0f);
		uniforms.SSAOParams = glm::vec4(0.002f, 1.0f, 0.0f, 0.0f);

		commandList->writeBuffer(m_CameraBuffer, &uniforms, sizeof(uniforms), 0);

		// Upload ShadowMapParams (shadow map size for PCF).
		{
			ShadowMapParams shadowParams;
			shadowParams.ShadowMapSize = m_ShadowMapSize;
			commandList->writeBuffer(m_ShadowParamsBuffer, &shadowParams, sizeof(shadowParams), 0);
		}

		// Clear colour and depth so an empty scene still looks intentional.
		auto& attachments = framebuffer->getDesc().colorAttachments;
		if (attachments.size() > 0)
		{
			commandList->clearTextureFloat(attachments[0].texture,
				nvrhi::TextureSubresourceSet(0, 1, 0, 1),
				nvrhi::Color(0.05f, 0.06f, 0.09f, 1.0f));
		}

		if (m_DepthTexture)
		{
			commandList->clearDepthStencilTexture(m_DepthTexture,
				nvrhi::TextureSubresourceSet(0, 1, 0, 1), true, 1.0f, false, 0);
		}
	}

	const PbrRenderer::ObjectDraw* PbrRenderer::PrepareMesh(const Ref<Mesh>& mesh)
	{
		if (!mesh)
			return nullptr;

		const Mesh* key = mesh.get();
		auto cached = m_DrawCache.find(key);
		if (cached != m_DrawCache.end())
			return &cached->second;

		auto device = VulkanRendererAPI::GetStaticDevice();
		auto vertexArray = std::dynamic_pointer_cast<VulkanVertexArray>(mesh->GetVertexArray());
		if (!device || !vertexArray)
		{
			FG_CORE_ERROR("PbrRenderer: mesh does not have a GPU vertex array");
			return nullptr;
		}

		ObjectDraw draw;

		// Resolve the input layout against the PBR vertex shader.
		VulkanShader* pbrShader = nullptr;
		if (auto shader = std::dynamic_pointer_cast<VulkanShader>(Renderer::GetShaderLibrary().Get("PBR")))
			pbrShader = shader.get();

		if (!pbrShader || !vertexArray->UpdateLayout(pbrShader, kPbrAttributes))
			return nullptr;

		draw.inputLayout = vertexArray->GetInputLayout();
		draw.vertexBinding = vertexArray->GetVertexBufferBinding(0);

		if (vertexArray->HasIndexBuffer())
		{
			draw.indexBinding = vertexArray->GetIndexBufferBinding();
			draw.indexed = true;
			// nvrhi's IndexBufferBinding has no count; take it from the mesh.
			draw.indexCount = mesh->GetIndexCount();
		}
		else
		{
			FG_CORE_ERROR("PbrRenderer: mesh has no index buffer; only indexed geometry is drawn");
			return nullptr;
		}

		// A dedicated uniform buffer for this object.
		nvrhi::BufferDesc objectDesc;
		objectDesc.byteSize = kConstantBufferAlignment;
		objectDesc.isConstantBuffer = true;
		objectDesc.debugName = "PbrRenderer.Object";
		objectDesc.keepInitialState = true;
		objectDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		draw.objectBuffer = device->createBuffer(objectDesc);

		// The binding set holds the per-frame resources plus this object's block.
		// The object block is rewritten before each of this object's draws.
		// Combined samplers (sampler2D, samplerCube) include both texture and sampler.
		nvrhi::BindingSetDesc setDesc;
		setDesc.bindings.push_back(MakeConstantBufferItem(m_CameraBuffer.Get(), 0));
		setDesc.bindings.push_back(MakeConstantBufferItem(draw.objectBuffer.Get(), 1));

		// Binding 2: combined sampler2D u_AlbedoTexture.
		{
			nvrhi::BindingSetItem item{};
			item.resourceHandle = m_WhiteTexture.Get();
			item.slot = 2;
			item.type = nvrhi::ResourceType::Texture_SRV;
			item.dimension = nvrhi::TextureDimension::Texture2D;
			item.format = m_WhiteTexture->getDesc().format;
			item.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
			setDesc.bindings.push_back(item);
			setDesc.bindings.push_back(MakeSamplerItem(m_LinearSampler.Get(), 2));
		}

		// Binding 3: combined sampler2D u_ShadowMap.
		{
			nvrhi::BindingSetItem item{};
			nvrhi::ITexture* shadowTex = m_ShadowMap ? m_ShadowMap.Get() : m_ShadowTexture.Get();
			item.resourceHandle = shadowTex;
			item.slot = 3;
			item.type = nvrhi::ResourceType::Texture_SRV;
			item.dimension = nvrhi::TextureDimension::Texture2D;
			item.format = shadowTex->getDesc().format;
			item.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
			setDesc.bindings.push_back(item);
			setDesc.bindings.push_back(MakeSamplerItem(m_ShadowSampler.Get(), 3));
		}

		// Binding 4: separate sampler (unused in PBR, kept for compatibility).
		setDesc.bindings.push_back(MakeSamplerItem(m_LinearSampler.Get(), 4));

		// Binding 5: ShadowMapParams uniform buffer.
		setDesc.bindings.push_back(MakeConstantBufferItem(m_ShadowParamsBuffer.Get(), 5));

		// Binding 6: combined samplerCube u_EnvMap.
		{
			nvrhi::BindingSetItem item{};
			item.resourceHandle = m_EnvMapFallback.Get();
			item.slot = 6;
			item.type = nvrhi::ResourceType::Texture_SRV;
			item.dimension = nvrhi::TextureDimension::TextureCube;
			item.format = m_EnvMapFallback->getDesc().format;
			item.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
			setDesc.bindings.push_back(item);
			setDesc.bindings.push_back(MakeSamplerItem(m_LinearSampler.Get(), 6));
		}

		// Binding 7: combined samplerCube u_IrradianceMap.
		{
			nvrhi::BindingSetItem item{};
			item.resourceHandle = m_IrradianceMap ? m_IrradianceMap.Get() : m_EnvMapFallback.Get();
			item.slot = 7;
			item.type = nvrhi::ResourceType::Texture_SRV;
			item.dimension = nvrhi::TextureDimension::TextureCube;
			item.format = m_IrradianceMap ? m_IrradianceMap->getDesc().format : m_EnvMapFallback->getDesc().format;
			item.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
			setDesc.bindings.push_back(item);
			setDesc.bindings.push_back(MakeSamplerItem(m_LinearSampler.Get(), 7));
		}

		draw.bindingSet = device->createBindingSet(setDesc, m_FrameLayout);
		if (!draw.bindingSet)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create a binding set for a mesh");
			return nullptr;
		}

		auto [it, inserted] = m_DrawCache.emplace(key, draw);
		(void)inserted;
		return &it->second;
	}

	void PbrRenderer::DrawObject(nvrhi::ICommandList* commandList,
		nvrhi::FramebufferHandle framebuffer,
		const Ref<Mesh>& mesh,
		const glm::mat4& transform,
		const glm::mat3& normalMatrix,
		const glm::vec3& albedo,
		float metallic,
		float roughness,
		float ambientOcclusion,
		const glm::vec3& emissive,
		float emissiveIntensity)
	{
		if (!commandList || !framebuffer || !IsValid())
			return;

		const ObjectDraw* draw = PrepareMesh(mesh);
		if (!draw)
			return;

		// Upload this object's uniforms before the draw that reads them.
		ObjectUniforms uniforms{};
		uniforms.Transform = transform;
		uniforms.NormalMatrix = glm::mat4(normalMatrix);
		uniforms.Albedo = glm::vec4(albedo, 1.0f);
		uniforms.MetallicRoughnessAO = glm::vec4(metallic, roughness, ambientOcclusion, 0.0f);
		uniforms.Emissive = glm::vec4(emissive, emissiveIntensity);
		commandList->writeBuffer(draw->objectBuffer, &uniforms, sizeof(uniforms), 0);

		nvrhi::GraphicsState state;
		state.framebuffer = framebuffer.Get();

		// The viewport and scissor cover the whole swapchain image.
		nvrhi::Viewport viewport(0.0f, 0.0f, m_FrameWidth, m_FrameHeight, 0.0f, 1.0f);
		state.viewport.addViewportAndScissorRect(viewport);

		// The input layout is baked into the pipeline, so use the pipeline built
		// for this mesh's layout.
		state.pipeline = GetPipelineForLayout(draw->inputLayout.Get());
		if (!state.pipeline)
			return;

		state.vertexBuffers.push_back(draw->vertexBinding);

		// Geometry uploads are recorded too, so the first frame's data lands
		// before this draw reads it.
		auto vertexArray = std::dynamic_pointer_cast<VulkanVertexArray>(mesh->GetVertexArray());
		if (vertexArray)
			vertexArray->UploadTo(commandList);

		state.indexBuffer = draw->indexBinding;
		state.bindings.push_back(draw->bindingSet.Get());

		commandList->setGraphicsState(state);

		// Only indexed geometry is submitted; a mesh without an index buffer
		// was rejected in PrepareMesh().
		nvrhi::DrawArguments args;
		args.vertexCount = draw->indexCount;
		args.instanceCount = 1;

		static bool loggedOnce = false;
		if (!loggedOnce)
		{
			loggedOnce = true;
			FG_CORE_INFO("DrawObject: {0} indices, vertex stride {1}, layout {2}",
				draw->indexCount, vertexArray ? vertexArray->GetStride() : 0,
				(bool)draw->inputLayout);
		}

		commandList->drawIndexed(args);
	}

	// --- Shadow pass implementation ---

	bool PbrRenderer::CreateShadowMap(uint32_t width, uint32_t height)
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device || width == 0 || height == 0)
			return false;

		nvrhi::TextureDesc shadowDesc;
		shadowDesc.width = width;
		shadowDesc.height = height;
		shadowDesc.format = nvrhi::Format::D32;
		shadowDesc.isRenderTarget = true;
		shadowDesc.isShaderResource = true;
		shadowDesc.debugName = "PbrRenderer.ShadowMap";
		shadowDesc.keepInitialState = true;
		shadowDesc.initialState = nvrhi::ResourceStates::RenderTarget;

		m_ShadowMap = device->createTexture(shadowDesc);
		if (!m_ShadowMap)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create shadow map texture");
			return false;
		}

		m_ShadowMapWidth = width;
		m_ShadowMapHeight = height;

		// Create a depth-only framebuffer for the shadow pass.
		nvrhi::FramebufferDesc shadowFbDesc;
		shadowFbDesc.setDepthAttachment(m_ShadowMap.Get());
		m_ShadowFramebuffer = device->createFramebuffer(shadowFbDesc);
		if (!m_ShadowFramebuffer)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create shadow map framebuffer");
			return false;
		}

		FG_CORE_INFO("Shadow map created: {0}x{1}", width, height);
		return true;
	}

	bool PbrRenderer::CreateShadowBindingLayout()
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return false;

		// Shadow shader binding layout:
		// binding 1: ObjectData (per-object transform)
		// binding 4: ShadowData (per-frame shadow view-projection)
		nvrhi::BindingLayoutDesc shadowLayoutDesc;
		shadowLayoutDesc.visibility = nvrhi::ShaderType::All;
		shadowLayoutDesc.bindings = {
			{ 1, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
			{ 4, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
		};
		m_ShadowLayout = device->createBindingLayout(shadowLayoutDesc);
		if (!m_ShadowLayout)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create shadow binding layout");
			return false;
		}

		return true;
	}

	bool PbrRenderer::CreateShadowResources()
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return false;

		// Shadow camera buffer (per-frame shadow view-projection).
		nvrhi::BufferDesc shadowCamDesc;
		shadowCamDesc.byteSize = kConstantBufferAlignment;
		shadowCamDesc.isConstantBuffer = true;
		shadowCamDesc.debugName = "PbrRenderer.ShadowCamera";
		shadowCamDesc.keepInitialState = true;
		shadowCamDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		m_ShadowCameraBuffer = device->createBuffer(shadowCamDesc);

		if (!m_ShadowCameraBuffer)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create shadow camera buffer");
			return false;
		}

		// Linear sampler for shadow map depth comparison.
		nvrhi::SamplerDesc shadowSamplerDesc;
		shadowSamplerDesc.addressU = nvrhi::SamplerAddressMode::ClampToBorder;
		shadowSamplerDesc.addressV = nvrhi::SamplerAddressMode::ClampToBorder;
		shadowSamplerDesc.addressW = nvrhi::SamplerAddressMode::ClampToBorder;
		shadowSamplerDesc.setMinFilter(true);
		shadowSamplerDesc.setMagFilter(true);
		shadowSamplerDesc.setMipFilter(true);
		shadowSamplerDesc.setBorderColor(nvrhi::Color(1.0f, 1.0f, 1.0f, 1.0f));
		m_ShadowSampler = device->createSampler(shadowSamplerDesc);

		return true;
	}

	bool PbrRenderer::CreateShadowPipelines()
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return false;

		// The shadow shader needs the Shadow vertex/fragment shaders.
		nvrhi::ShaderHandle vertexShader, fragmentShader;
		if (auto shader = std::dynamic_pointer_cast<VulkanShader>(
				Renderer::GetShaderLibrary().Get("Shadow")))
		{
			vertexShader = shader->GetVertexShader();
			fragmentShader = shader->GetFragmentShader();
		}

		if (!vertexShader || !fragmentShader)
		{
			FG_CORE_ERROR("PbrRenderer: the Shadow shader is not available");
			return false;
		}

		m_ShadowVertexShader = vertexShader;
		m_ShadowFragmentShader = fragmentShader;
		return true;
	}

	nvrhi::IGraphicsPipeline* PbrRenderer::GetShadowPipelineForLayout(nvrhi::IInputLayout* inputLayout)
	{
		if (!inputLayout || !m_ShadowVertexShader || !m_ShadowFragmentShader)
			return nullptr;

		auto cached = m_ShadowPipelineCache.find(inputLayout);
		if (cached != m_ShadowPipelineCache.end())
			return cached->second.Get();

		auto device = VulkanRendererAPI::GetStaticDevice();

		nvrhi::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.VS = m_ShadowVertexShader;
		pipelineDesc.PS = m_ShadowFragmentShader;
		pipelineDesc.inputLayout = inputLayout;
		pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;

		pipelineDesc.renderState.depthStencilState.depthTestEnable = true;
		pipelineDesc.renderState.depthStencilState.depthWriteEnable = true;
		pipelineDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Less;

		pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
		pipelineDesc.renderState.rasterState.frontCounterClockwise = true;

		pipelineDesc.bindingLayouts.push_back(m_ShadowLayout);

		nvrhi::FramebufferInfo framebufferInfo;
		// depth-only: leave colorFormats empty (default-constructed static_vector)
		framebufferInfo.depthFormat = nvrhi::Format::D32;
		framebufferInfo.sampleCount = 1;

		nvrhi::GraphicsPipelineHandle pipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
		if (!pipeline)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create a shadow pipeline for a vertex layout");
			return nullptr;
		}

		m_ShadowPipelineCache.emplace(inputLayout, pipeline);
		return pipeline.Get();
	}

	const PbrRenderer::ShadowDraw* PbrRenderer::PrepareShadowMesh(const Ref<Mesh>& mesh)
	{
		if (!mesh)
			return nullptr;

		const Mesh* key = mesh.get();
		auto cached = m_ShadowDrawCache.find(key);
		if (cached != m_ShadowDrawCache.end())
			return &cached->second;

		auto device = VulkanRendererAPI::GetStaticDevice();
		auto vertexArray = std::dynamic_pointer_cast<VulkanVertexArray>(mesh->GetVertexArray());
		if (!device || !vertexArray)
		{
			FG_CORE_ERROR("PbrRenderer: shadow mesh does not have a GPU vertex array");
			return nullptr;
		}

		ShadowDraw draw;

		// Resolve the input layout against the shadow vertex shader.
		VulkanShader* shadowShader = nullptr;
		if (auto shader = std::dynamic_pointer_cast<VulkanShader>(Renderer::GetShaderLibrary().Get("Shadow")))
			shadowShader = shader.get();

		if (!shadowShader || !vertexArray->UpdateLayout(shadowShader, kPbrAttributes))
			return nullptr;

		draw.inputLayout = vertexArray->GetInputLayout();
		draw.vertexBinding = vertexArray->GetVertexBufferBinding(0);

		if (vertexArray->HasIndexBuffer())
		{
			draw.indexBinding = vertexArray->GetIndexBufferBinding();
			draw.indexed = true;
			draw.indexCount = mesh->GetIndexCount();
		}
		else
		{
			FG_CORE_ERROR("PbrRenderer: shadow mesh has no index buffer");
			return nullptr;
		}

		// Per-object uniform buffer for the shadow pass.
		nvrhi::BufferDesc objectDesc;
		objectDesc.byteSize = kConstantBufferAlignment;
		objectDesc.isConstantBuffer = true;
		objectDesc.debugName = "PbrRenderer.ShadowObject";
		objectDesc.keepInitialState = true;
		objectDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		draw.objectBuffer = device->createBuffer(objectDesc);

		// Binding set: binding 1 = object buffer, binding 4 = shadow camera buffer.
		nvrhi::BindingSetDesc setDesc;
		setDesc.bindings.push_back(MakeConstantBufferItem(draw.objectBuffer.Get(), 1));
		setDesc.bindings.push_back(MakeConstantBufferItem(m_ShadowCameraBuffer.Get(), 4));

		draw.bindingSet = device->createBindingSet(setDesc, m_ShadowLayout);
		if (!draw.bindingSet)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create a shadow binding set for a mesh");
			return nullptr;
		}

		auto [it, inserted] = m_ShadowDrawCache.emplace(key, draw);
		(void)inserted;
		return &it->second;
	}

	void PbrRenderer::UpdateShadowFrame(nvrhi::ICommandList* commandList,
		nvrhi::FramebufferHandle framebuffer,
		const glm::mat4& shadowViewProjection)
	{
		if (!commandList || !framebuffer)
			return;

		// Record the shadow camera uniform for this frame.
		glm::mat4 shadowCameraData = shadowViewProjection;
		commandList->writeBuffer(m_ShadowCameraBuffer, &shadowCameraData, sizeof(shadowCameraData), 0);

		// Clear depth to 1.0 (far plane).
		if (m_ShadowMap)
		{
			commandList->clearDepthStencilTexture(m_ShadowMap,
				nvrhi::TextureSubresourceSet(0, 1, 0, 1), true, 1.0f, false, 0);
		}
	}

	void PbrRenderer::DrawShadowObject(nvrhi::ICommandList* commandList,
		nvrhi::FramebufferHandle framebuffer,
		const Ref<Mesh>& mesh,
		const glm::mat4& transform)
	{
		if (!commandList || !framebuffer || !m_ShadowVertexShader || !m_ShadowFragmentShader)
			return;

		const ShadowDraw* draw = PrepareShadowMesh(mesh);
		if (!draw)
			return;

		// Upload this object's transform before the draw that reads it.
		commandList->writeBuffer(draw->objectBuffer, &transform, sizeof(transform), 0);

		nvrhi::GraphicsState state;
		state.framebuffer = framebuffer.Get();

		nvrhi::Viewport viewport(0.0f, 0.0f, static_cast<float>(m_ShadowMapWidth), static_cast<float>(m_ShadowMapHeight), 0.0f, 1.0f);
		state.viewport.addViewportAndScissorRect(viewport);

		state.pipeline = GetShadowPipelineForLayout(draw->inputLayout.Get());
		if (!state.pipeline)
			return;

		state.vertexBuffers.push_back(draw->vertexBinding);

		// Geometry uploads are recorded too.
		auto vertexArray = std::dynamic_pointer_cast<VulkanVertexArray>(mesh->GetVertexArray());
		if (vertexArray)
			vertexArray->UploadTo(commandList);

		state.indexBuffer = draw->indexBinding;
		state.bindings.push_back(draw->bindingSet.Get());

		commandList->setGraphicsState(state);

		nvrhi::DrawArguments args;
		args.vertexCount = draw->indexCount;
		args.instanceCount = 1;

		commandList->drawIndexed(args);
	}

	bool PbrRenderer::ResizeShadowMap(uint32_t width, uint32_t height)
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device || width == 0 || height == 0)
			return false;

		if (m_ShadowMap && m_ShadowMapWidth == width && m_ShadowMapHeight == height)
			return true;

		nvrhi::TextureDesc shadowDesc;
		shadowDesc.width = width;
		shadowDesc.height = height;
		shadowDesc.format = nvrhi::Format::D32;
		shadowDesc.isRenderTarget = true;
		shadowDesc.isShaderResource = true;
		shadowDesc.debugName = "PbrRenderer.ShadowMap";
		shadowDesc.keepInitialState = true;
		shadowDesc.initialState = nvrhi::ResourceStates::RenderTarget;

		m_ShadowMap = device->createTexture(shadowDesc);
		if (!m_ShadowMap)
			return false;

		m_ShadowMapWidth = width;
		m_ShadowMapHeight = height;
		m_ShadowMapSize = glm::vec2(static_cast<float>(width), static_cast<float>(height));
		return true;
	}

	void PbrRenderer::SetHDRFramebuffer(nvrhi::FramebufferHandle framebuffer, nvrhi::TextureHandle hdrTexture, nvrhi::TextureHandle depthTexture)
	{
		m_HDRFramebuffer = framebuffer;
		m_HDRTexture = hdrTexture;
		m_DepthTexture = depthTexture;
	}

	bool PbrRenderer::LoadEnvironmentMap(nvrhi::TextureHandle envMap)
	{
		m_EnvMap = envMap;
		FG_CORE_INFO("PbrRenderer: environment map loaded");

		// Generate IBL maps from the environment map.
		if (!CreateIrradianceMap())
		{
			FG_CORE_WARN("PbrRenderer: failed to create irradiance map");
		}
		if (!CreatePrefilterMap(7)) // 7 mip levels for roughness 0..1
		{
			FG_CORE_WARN("PbrRenderer: failed to create prefilter map");
		}

		return true;
	}

	bool PbrRenderer::CreateIBLFullscreenQuad()
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return false;

		// Fullscreen quad in NDC space.
		struct Vertex
		{
			glm::vec2 position;
			glm::vec2 uv;
		};

		const Vertex quad[4] = {
			{ {-1.0f, -1.0f}, {0.0f, 0.0f} },
			{ { 1.0f, -1.0f}, {1.0f, 0.0f} },
			{ { 1.0f,  1.0f}, {1.0f, 1.0f} },
			{ {-1.0f,  1.0f}, {0.0f, 1.0f} }
		};

		nvrhi::BufferDesc vboDesc;
		vboDesc.byteSize = sizeof(quad);
		vboDesc.isConstantBuffer = false;
		vboDesc.debugName = "PbrRenderer.IBLQuadVBO";
		vboDesc.keepInitialState = true;
		vboDesc.initialState = nvrhi::ResourceStates::VertexBuffer;

		nvrhi::BufferHandle vbo = device->createBuffer(vboDesc);
		if (!vbo)
			return false;

		// Upload vertices.
		nvrhi::CommandListParameters params;
		nvrhi::CommandListHandle cmd = device->createCommandList(params);
		cmd->open();
		cmd->writeBuffer(vbo, quad, sizeof(quad), 0);
		cmd->close();
		device->executeCommandList(cmd, nvrhi::CommandQueue::Graphics);
		cmd->Release();

		// Store the vertex buffer for later use.
		m_IBLQuadVBO = vbo;
		m_IBLQuadVertexBinding = { vbo.Get(), 0, sizeof(Vertex) };

		FG_CORE_INFO("IBL: fullscreen quad created");
		return true;
	}

	bool PbrRenderer::CreateIBLShaders()
	{
		// Load irradiance shader.
		auto irrShader = Renderer::GetShaderLibrary().Get("Irradiance");
		if (!irrShader)
		{
			FG_CORE_WARN("IBL: irradiance shader not loaded");
			return false;
		}
		auto vulkanIrr = std::dynamic_pointer_cast<VulkanShader>(irrShader);
		if (!vulkanIrr || !vulkanIrr->GetVertexShader() || !vulkanIrr->GetFragmentShader())
		{
			FG_CORE_WARN("IBL: irradiance shader not properly compiled");
			return false;
		}
		m_IrradianceVertexShader = vulkanIrr->GetVertexShader();
		m_IrradianceFragmentShader = vulkanIrr->GetFragmentShader();

		// Load prefilter shader.
		auto prefShader = Renderer::GetShaderLibrary().Get("Prefilter");
		if (!prefShader)
		{
			FG_CORE_WARN("IBL: prefilter shader not loaded");
			return false;
		}
		auto vulkanPref = std::dynamic_pointer_cast<VulkanShader>(prefShader);
		if (!vulkanPref || !vulkanPref->GetVertexShader() || !vulkanPref->GetFragmentShader())
		{
			FG_CORE_WARN("IBL: prefilter shader not properly compiled");
			return false;
		}
		m_PrefilterVertexShader = vulkanPref->GetVertexShader();
		m_PrefilterFragmentShader = vulkanPref->GetFragmentShader();

		FG_CORE_INFO("IBL: shaders loaded");
		return true;
	}

	bool PbrRenderer::CreateIBLBindingLayout()
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return false;

		// IBL layout: binding 0 = sampler2D env map, binding 1 = params uniform buffer.
		nvrhi::BindingLayoutDesc iblLayoutDesc;
		iblLayoutDesc.visibility = nvrhi::ShaderType::All;
		iblLayoutDesc.bindings = {
			{ 0, nvrhi::ResourceType::Texture_SRV, 0, 1 },
			{ 1, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
		};
		m_IBLBindingLayout = device->createBindingLayout(iblLayoutDesc);
		if (!m_IBLBindingLayout)
		{
			FG_CORE_ERROR("IBL: failed to create binding layout");
			return false;
		}

		// IBL params buffer.
		nvrhi::BufferDesc paramsDesc;
		paramsDesc.byteSize = 64; // Enough for both IrradianceParams and PrefilterParams
		paramsDesc.isConstantBuffer = true;
		paramsDesc.debugName = "PbrRenderer.IBLParams";
		paramsDesc.keepInitialState = true;
		paramsDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		m_IBLParamsBuffer = device->createBuffer(paramsDesc);
		if (!m_IBLParamsBuffer)
		{
			FG_CORE_ERROR("IBL: failed to create params buffer");
			return false;
		}

		FG_CORE_INFO("IBL: binding layout created");
		return true;
	}

	bool PbrRenderer::CreateIBLPipelines()
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return false;

		// Irradiance pipeline.
		{
			nvrhi::GraphicsPipelineDesc pipelineDesc;
			pipelineDesc.VS = m_IrradianceVertexShader;
			pipelineDesc.PS = m_IrradianceFragmentShader;
			pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
			pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
			pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
			pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
			pipelineDesc.renderState.rasterState.frontCounterClockwise = true;
			pipelineDesc.bindingLayouts.push_back(m_IBLBindingLayout);

			// Irradiance output: RGBA16_FLOAT cubemap face.
			nvrhi::FramebufferInfo fbInfo;
			fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT);
			fbInfo.depthFormat = nvrhi::Format::UNKNOWN;
			fbInfo.sampleCount = 1;

			m_IrradiancePipeline = device->createGraphicsPipeline(pipelineDesc, fbInfo);
			if (!m_IrradiancePipeline)
			{
				FG_CORE_ERROR("IBL: failed to create irradiance pipeline");
				return false;
			}
		}

		// Prefilter pipeline.
		{
			nvrhi::GraphicsPipelineDesc pipelineDesc;
			pipelineDesc.VS = m_PrefilterVertexShader;
			pipelineDesc.PS = m_PrefilterFragmentShader;
			pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;
			pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
			pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;
			pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
			pipelineDesc.renderState.rasterState.frontCounterClockwise = true;
			pipelineDesc.bindingLayouts.push_back(m_IBLBindingLayout);

			// Prefilter output: RGBA16_FLOAT cubemap face.
			nvrhi::FramebufferInfo fbInfo;
			fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT);
			fbInfo.depthFormat = nvrhi::Format::UNKNOWN;
			fbInfo.sampleCount = 1;

			m_PrefilterPipeline = device->createGraphicsPipeline(pipelineDesc, fbInfo);
			if (!m_PrefilterPipeline)
			{
				FG_CORE_ERROR("IBL: failed to create prefilter pipeline");
				return false;
			}
		}

		FG_CORE_INFO("IBL: pipelines created");
		return true;
	}

	bool PbrRenderer::RenderIrradianceMap()
	{
		if (!m_EnvMap || !m_IrradiancePipeline || !m_IBLBindingLayout)
			return false;

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return false;

		const int faceCount = 6;
		const int size = 256; // Irradiance cubemap resolution

		// Create irradiance cubemap (6 faces, 1 mip level).
		nvrhi::TextureDesc irradianceDesc;
		irradianceDesc.width = size;
		irradianceDesc.height = size;
		irradianceDesc.depth = 1;
		irradianceDesc.arraySize = 6;
		irradianceDesc.mipLevels = 1;
		irradianceDesc.format = nvrhi::Format::RGBA16_FLOAT;
		irradianceDesc.isShaderResource = true;
		irradianceDesc.isRenderTarget = true;
		irradianceDesc.dimension = nvrhi::TextureDimension::TextureCube;
		irradianceDesc.debugName = "PbrRenderer.IrradianceMap";
		irradianceDesc.keepInitialState = true;
		irradianceDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		m_IrradianceMap = device->createTexture(irradianceDesc);
		if (!m_IrradianceMap)
		{
			FG_CORE_ERROR("IBL: failed to create irradiance cubemap");
			return false;
		}

		// Create irradiance framebuffer (6 faces).
		nvrhi::FramebufferDesc irradianceFbDesc;
		for (int i = 0; i < faceCount; i++)
		{
			nvrhi::TextureSubresourceSet subres(0, 1, i, 1);
			irradianceFbDesc.addColorAttachment(m_IrradianceMap.Get(), subres);
		}
		m_IrradianceFramebuffer = device->createFramebuffer(irradianceFbDesc);
		if (!m_IrradianceFramebuffer)
		{
			FG_CORE_ERROR("IBL: failed to create irradiance framebuffer");
			return false;
		}

		// Render each face.
		nvrhi::CommandListParameters params;
		nvrhi::CommandListHandle cmd = device->createCommandList(params);
		cmd->open();

		// Create IBL binding set (env map + params).
		nvrhi::BindingSetDesc iblSetDesc;
		{
			nvrhi::BindingSetItem texItem{};
			texItem.resourceHandle = m_EnvMap.Get();
			texItem.slot = 0;
			texItem.type = nvrhi::ResourceType::Texture_SRV;
			texItem.dimension = nvrhi::TextureDimension::Texture2D;
			texItem.format = m_EnvMap->getDesc().format;
			texItem.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
			iblSetDesc.bindings.push_back(texItem);
		}
		{
			nvrhi::BindingSetItem bufItem{};
			bufItem.resourceHandle = m_IBLParamsBuffer.Get();
			bufItem.slot = 1;
			bufItem.type = nvrhi::ResourceType::ConstantBuffer;
			bufItem.range = nvrhi::BufferRange(0, 0);
			iblSetDesc.bindings.push_back(bufItem);
		}
		nvrhi::BindingSetHandle iblSet = device->createBindingSet(iblSetDesc, m_IBLBindingLayout);
		if (!iblSet)
		{
			FG_CORE_ERROR("IBL: failed to create IBL binding set");
			cmd->Release();
			return false;
		}

		for (int face = 0; face < faceCount; face++)
		{
			// Set the irradiance params (normal direction for this face).
			glm::vec3 faceNormals[6] = {
				{ 1.0f, 0.0f, 0.0f },  // +X
				{ -1.0f, 0.0f, 0.0f }, // -X
				{ 0.0f, 1.0f, 0.0f },  // +Y
				{ 0.0f, -1.0f, 0.0f }, // -Y
				{ 0.0f, 0.0f, 1.0f },  // +Z
				{ 0.0f, 0.0f, -1.0f }  // -Z
			};

			// Write irradiance params: normal (vec3) + radius (float) = 16 bytes
			glm::vec4 irradianceParams(faceNormals[face], 1.0f);
			cmd->writeBuffer(m_IBLParamsBuffer, &irradianceParams, sizeof(irradianceParams), 0);

			nvrhi::GraphicsState state;
			state.framebuffer = m_IrradianceFramebuffer.Get();

			nvrhi::Viewport viewport(0.0f, 0.0f, static_cast<float>(size), static_cast<float>(size), 0.0f, 1.0f);
			state.viewport.addViewportAndScissorRect(viewport);
			state.pipeline = m_IrradiancePipeline.Get();
			state.bindings.push_back(iblSet.Get());
			state.vertexBuffers.push_back(m_IBLQuadVertexBinding);

			cmd->setGraphicsState(state);

			// Draw fullscreen quad.
			nvrhi::DrawArguments args;
			args.vertexCount = 4;
			args.instanceCount = 1;
			cmd->draw(args);
		}

		cmd->close();
		device->executeCommandList(cmd, nvrhi::CommandQueue::Graphics);
		cmd->Release();

		FG_CORE_INFO("IBL: irradiance map generated ({0}x{1})", size, size);
		return true;
	}

	bool PbrRenderer::RenderPrefilterMap(uint32_t mipCount)
	{
		if (!m_EnvMap || !m_PrefilterPipeline || !m_IBLBindingLayout)
			return false;

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return false;

		const int faceCount = 6;
		const int baseSize = 512; // Prefilter cubemap base resolution

		// Create prefilter cubemap with mip levels.
		nvrhi::TextureDesc prefDesc;
		prefDesc.width = baseSize;
		prefDesc.height = baseSize;
		prefDesc.depth = 1;
		prefDesc.arraySize = 6;
		prefDesc.mipLevels = mipCount;
		prefDesc.format = nvrhi::Format::RGBA16_FLOAT;
		prefDesc.isShaderResource = true;
		prefDesc.isRenderTarget = true;
		prefDesc.dimension = nvrhi::TextureDimension::TextureCube;
		prefDesc.debugName = "PbrRenderer.PrefilterMap";
		prefDesc.keepInitialState = true;
		prefDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		m_PrefilterMap = device->createTexture(prefDesc);
		if (!m_PrefilterMap)
		{
			FG_CORE_ERROR("IBL: failed to create prefilter cubemap");
			return false;
		}
		m_PrefilterMipCount = mipCount;

		// Create prefilter framebuffer (base mip level, 6 faces).
		nvrhi::FramebufferDesc prefFbDesc;
		for (int i = 0; i < faceCount; i++)
		{
			nvrhi::TextureSubresourceSet subres(0, 1, i, 1);
			prefFbDesc.addColorAttachment(m_PrefilterMap.Get(), subres);
		}
		m_PrefilterFramebuffer = device->createFramebuffer(prefFbDesc);
		if (!m_PrefilterFramebuffer)
		{
			FG_CORE_ERROR("IBL: failed to create prefilter framebuffer");
			return false;
		}

		// Create IBL binding set.
		nvrhi::BindingSetDesc iblSetDesc;
		{
			nvrhi::BindingSetItem texItem{};
			texItem.resourceHandle = m_EnvMap.Get();
			texItem.slot = 0;
			texItem.type = nvrhi::ResourceType::Texture_SRV;
			texItem.dimension = nvrhi::TextureDimension::Texture2D;
			texItem.format = m_EnvMap->getDesc().format;
			texItem.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
			iblSetDesc.bindings.push_back(texItem);
		}
		{
			nvrhi::BindingSetItem bufItem{};
			bufItem.resourceHandle = m_IBLParamsBuffer.Get();
			bufItem.slot = 1;
			bufItem.type = nvrhi::ResourceType::ConstantBuffer;
			bufItem.range = nvrhi::BufferRange(0, 0);
			iblSetDesc.bindings.push_back(bufItem);
		}
		nvrhi::BindingSetHandle iblSet = device->createBindingSet(iblSetDesc, m_IBLBindingLayout);
		if (!iblSet)
		{
			FG_CORE_ERROR("IBL: failed to create IBL binding set");
			return false;
		}

		// Render each mip level.
		nvrhi::CommandListParameters params;
		nvrhi::CommandListHandle cmd = device->createCommandList(params);
		cmd->open();

		for (uint32_t mip = 0; mip < mipCount; mip++)
		{
			// Roughness for this mip level (0.0 to ~1.0).
			float roughness = static_cast<float>(mip) / static_cast<float>(mipCount - 1);

			// Write prefilter params: reflection vector (vec3) + roughness (float).
			glm::vec4 prefParams(glm::vec3(0.0f, 0.0f, 1.0f), roughness);
			cmd->writeBuffer(m_IBLParamsBuffer, &prefParams, sizeof(prefParams), 0);

			// Render to each face at this mip level.
			for (int face = 0; face < faceCount; face++)
			{
				// Update framebuffer to point to this mip level.
				nvrhi::TextureSubresourceSet subres(mip, 1, face, 1);
				nvrhi::FramebufferDesc mipFbDesc;
				mipFbDesc.addColorAttachment(m_PrefilterMap.Get(), subres);
				nvrhi::FramebufferHandle mipFb = device->createFramebuffer(mipFbDesc);

				nvrhi::Viewport viewport(0.0f, 0.0f,
					static_cast<float>(baseSize >> mip),
					static_cast<float>(baseSize >> mip),
					0.0f, 1.0f);

				nvrhi::GraphicsState state;
				state.framebuffer = mipFb.Get();
				state.viewport.addViewportAndScissorRect(viewport);
				state.pipeline = m_PrefilterPipeline.Get();
				state.bindings.push_back(iblSet.Get());
				state.vertexBuffers.push_back(m_IBLQuadVertexBinding);

				cmd->setGraphicsState(state);
				cmd->draw(nvrhi::DrawArguments{ 4, 1 });

				mipFb->Release();
			}
		}

		cmd->close();
		device->executeCommandList(cmd, nvrhi::CommandQueue::Graphics);
		cmd->Release();

		FG_CORE_INFO("IBL: prefilter map generated ({0} mip levels)", mipCount);
		return true;
	}

	bool PbrRenderer::CreateIrradianceMap()
	{
		if (!m_EnvMap)
		{
			FG_CORE_WARN("IBL: no environment map loaded, skipping irradiance");
			return true;
		}

		if (!m_IrradiancePipeline || !m_IBLBindingLayout)
		{
			FG_CORE_WARN("IBL: IBL infrastructure not initialized");
			return false;
		}

		return RenderIrradianceMap();
	}

	bool PbrRenderer::CreatePrefilterMap(uint32_t mipCount)
	{
		if (!m_EnvMap)
		{
			FG_CORE_WARN("IBL: no environment map loaded, skipping prefilter");
			return true;
		}

		if (!m_PrefilterPipeline || !m_IBLBindingLayout)
		{
			FG_CORE_WARN("IBL: IBL infrastructure not initialized");
			return false;
		}

		return RenderPrefilterMap(mipCount);
	}

}

#endif // FG_NO_VULKAN_RUNTIME
