#include "fgpch.h"
#include <cstdlib>
#include "Platform/Vulkan/PbrRenderer.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"
#include "Platform/Vulkan/VulkanSwapchain.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanVertexArray.h"
#include "Platform/Vulkan/VulkanVertexBuffer.h"
#include "Platform/Vulkan/VulkanIndexBuffer.h"
#include "Platform/Vulkan/VulkanTexture.h"

#include "Forge/Renderer/Mesh.h"
#include "Forge/Renderer/VertexArray.h"
#include "Forge/Renderer/ShaderLibrary.h"
#include "Forge/Renderer/Renderer.h"
#include "Forge/Renderer/Camera.h"

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
		// Built through nvrhi's factories rather than by assigning fields by
		// hand. BindingSetItem declares no default member initialisers, so a
		// hand-built item leaves fields at whatever was on the stack - and an
		// item with arrayElement 32767 does not match a layout declaring array
		// size 1, which fails the whole binding set and silently draws nothing.
		nvrhi::BindingSetItem MakeConstantBufferItem(nvrhi::IBuffer* buffer, uint32_t slot)
		{
			return nvrhi::BindingSetItem::ConstantBuffer(slot, buffer);
		}

		nvrhi::BindingSetItem MakeSamplerItem(nvrhi::ISampler* sampler, uint32_t slot)
		{
			return nvrhi::BindingSetItem::Sampler(slot, sampler);
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
		if (!CreatePipelines())	return false;

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

		// Set 0: camera block, object block, albedo texture + sampler, shadow map
		// + sampler, linear sampler, shadow params, env map + sampler, irradiance
		// map + sampler.
		//
		// nvrhi adds `bindingOffsets` to every slot to get the Vulkan binding, and
		// its defaults are HLSL register spaces (t#, s#, b#, u#) that assume
		// DXC-generated SPIR-V. These shaders are compiled by glslang and declare
		// explicit GLSL binding numbers, so the offsets must be zeroed or every
		// descriptor lands at a binding number the shader never reads.
		nvrhi::BindingLayoutDesc frameLayoutDesc;
	frameLayoutDesc.visibility = nvrhi::ShaderType::All;
	frameLayoutDesc.setBindingOffsets(nvrhi::VulkanBindingOffsets{ 0, 0, 0, 0 });
	frameLayoutDesc.bindings = {
		{ 0, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
		{ 1, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
		{ 2, nvrhi::ResourceType::Texture_SRV, 0, 1 },
		{ 3, nvrhi::ResourceType::Sampler, 0, 1 },
		{ 4, nvrhi::ResourceType::Texture_SRV, 0, 1 },
		{ 5, nvrhi::ResourceType::Sampler, 0, 1 },
		{ 6, nvrhi::ResourceType::Sampler, 0, 1 },
		{ 7, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
		{ 8, nvrhi::ResourceType::Texture_SRV, 0, 1 },
		{ 9, nvrhi::ResourceType::Sampler, 0, 1 },
		{ 10, nvrhi::ResourceType::Texture_SRV, 0, 1 },
		{ 11, nvrhi::ResourceType::Sampler, 0, 1 },
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
		if (!m_LinearSampler.Get())
		{
			FG_CORE_WARN("PbrRenderer: failed to create linear sampler");
			return false;
		}

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
		//
		// It has to be a real cube: bindings 8 and 10 declare
		// TextureDimension::TextureCube, and nvrhi rejects a binding whose
		// declared dimension does not match the texture's. A plain Texture2D
		// here invalidates the whole binding set, so PBR draws nothing at all.
		nvrhi::TextureDesc envFallbackDesc;
		envFallbackDesc.width = 1;
		envFallbackDesc.height = 1;
		envFallbackDesc.format = nvrhi::Format::RGBA16_FLOAT;
		envFallbackDesc.dimension = nvrhi::TextureDimension::TextureCube;
		envFallbackDesc.arraySize = 6;
		envFallbackDesc.isShaderResource = true;
		envFallbackDesc.debugName = "PbrRenderer.EnvFallback";
		envFallbackDesc.keepInitialState = true;
		envFallbackDesc.initialState = nvrhi::ResourceStates::ShaderResource;
		m_EnvMapFallback = device->createTexture(envFallbackDesc);

		// ShadowMapParams uniform buffer.
		//
		// A constant buffer binding must be a multiple of 256 bytes. At 32 the
		// binding is invalid and nvrhi drops it from the set, which fails the
		// whole binding set and therefore every draw.
		nvrhi::BufferDesc shadowParamsDesc;
		shadowParamsDesc.byteSize = kConstantBufferAlignment;
		shadowParamsDesc.isConstantBuffer = true;
		shadowParamsDesc.debugName = "PbrRenderer.ShadowParams";
		shadowParamsDesc.keepInitialState = true;
		shadowParamsDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
		m_ShadowParamsBuffer = device->createBuffer(shadowParamsDesc);

		return true;
	}

	bool PbrRenderer::CreatePipelines()
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

		// The swapchain framebuffers carry no depth attachment: the Final pass
		// and ImGui composite without depth testing, and a supersampled depth
		// would not match the swapchain extent. Depth lives only on the HDR
		// target the PBR pass draws into.

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

	const PbrRenderer::ObjectDraw* PbrRenderer::PrepareMesh(const Ref<Mesh>& mesh, const Ref<Texture>& albedoTexture,
		nvrhi::ITexture* envTexture)
	{
		if (!mesh)
			return nullptr;

		nvrhi::ITexture* albedoTex = nullptr;
		if (albedoTexture)
		{
			auto vulkanTex = std::dynamic_pointer_cast<VulkanTexture>(albedoTexture);
			if (vulkanTex && vulkanTex->GetTextureHandle())
				albedoTex = vulkanTex->GetTextureHandle();
		}

		ObjectDrawKey key;
		key.m_Mesh = mesh.get();
		key.m_AlbedoTexture = albedoTex;
		key.m_EnvTexture = envTexture ? envTexture : m_EnvMapFallback.Get();
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
		// Textures and samplers occupy separate bindings: the PBR shader declares
		// `texture2D`/`textureCube` plus a paired `sampler`, because nvrhi cannot
		// bind a combined image sampler.
		nvrhi::BindingSetDesc setDesc;
		setDesc.bindings.push_back(MakeConstantBufferItem(m_CameraBuffer.Get(), 0));
		setDesc.bindings.push_back(MakeConstantBufferItem(draw.objectBuffer.Get(), 1));

		// Binding 2: u_AlbedoTexture, binding 3: u_AlbedoSampler.
		{
			nvrhi::ITexture* albedoTex = key.m_AlbedoTexture ? key.m_AlbedoTexture : m_WhiteTexture.Get();
			setDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(2, albedoTex,
				albedoTex->getDesc().format,
				nvrhi::TextureSubresourceSet(0, 1, 0, 1),
				nvrhi::TextureDimension::Texture2D));
			setDesc.bindings.push_back(MakeSamplerItem(m_LinearSampler.Get(), 3));
		}

		// Binding 4: u_ShadowMap, binding 5: u_ShadowSampler.
		{
			nvrhi::ITexture* shadowTex = m_ShadowMap ? m_ShadowMap.Get() : m_ShadowTexture.Get();

			// Built through nvrhi's factory: BindingSetItem has no default member
			// initialisers, so a hand-built item leaves arrayElement and friends
			// uninitialised and the set is rejected.
			//
			// The SRV must name a concrete view format; an UNKNOWN value makes
			// nvrhi's Vulkan backend assert in convertFormat().
			setDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(4, shadowTex,
				shadowTex->getDesc().format,
				nvrhi::TextureSubresourceSet(0, 1, 0, 1),
				nvrhi::TextureDimension::Texture2D));
			setDesc.bindings.push_back(MakeSamplerItem(m_ShadowSampler.Get(), 5));
		}

		// Binding 6: separate sampler (unused in PBR, kept for compatibility).
		setDesc.bindings.push_back(MakeSamplerItem(m_LinearSampler.Get(), 6));

		// Binding 7: ShadowMapParams uniform buffer.
		setDesc.bindings.push_back(MakeConstantBufferItem(m_ShadowParamsBuffer.Get(), 7));

		// Binding 8: u_EnvMap, binding 9: u_EnvSampler.
		//
		// The SRV covers every slice of the cube: a single-slice range would
		// silently clamp all six reflection directions onto face 0.
		{
			nvrhi::ITexture* envTex = key.m_EnvTexture ? key.m_EnvTexture : m_EnvMapFallback.Get();
			const uint32_t envSlices = envTex->getDesc().dimension == nvrhi::TextureDimension::TextureCube
				? std::max(1u, envTex->getDesc().arraySize)
				: 1u;
			setDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(8, envTex,
				envTex->getDesc().format,
				nvrhi::TextureSubresourceSet(0, 1, 0, envSlices),
				nvrhi::TextureDimension::TextureCube));
			setDesc.bindings.push_back(MakeSamplerItem(m_LinearSampler.Get(), 9));
		}

		// Binding 10: u_IrradianceMap, binding 11: u_IrradianceSampler.
		{
			setDesc.bindings.push_back(nvrhi::BindingSetItem::Texture_SRV(10, m_IrradianceMap ? m_IrradianceMap.Get() : m_EnvMapFallback.Get(),
				m_IrradianceMap ? m_IrradianceMap->getDesc().format : m_EnvMapFallback->getDesc().format,
				nvrhi::TextureSubresourceSet(0, 1, 0, 1),
				nvrhi::TextureDimension::TextureCube));
			setDesc.bindings.push_back(MakeSamplerItem(m_LinearSampler.Get(), 11));
		}

		if (const char* skip = getenv("FG_PBR_SKIP"))
		{
			size_t n = (size_t)atoi(skip);
			if (n < setDesc.bindings.size())
				setDesc.bindings.erase(setDesc.bindings.begin() + n);
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
		const Ref<Texture>& albedoTexture,
		const glm::mat4& transform,
		const glm::mat3& normalMatrix,
		const glm::vec3& albedo,
		float metallic,
		float roughness,
		float ambientOcclusion,
		const glm::vec3& emissive,
		float emissiveIntensity,
		bool fromReflectionProbe)
	{
		if (!commandList || !framebuffer || !IsValid())
			return;

		// A draw into the probe must not sample the cubemap it is writing, so
		// probe draws keep the static fallback environment; every other draw
		// reflects the probe once it exists.
		nvrhi::ITexture* envTexture = m_EnvMapFallback.Get();
		if (!fromReflectionProbe && m_ReflectionProbeEnabled)
			envTexture = m_ReflectionMap.Get();

		const ObjectDraw* draw = PrepareMesh(mesh, albedoTexture, envTexture);
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

		// The viewport and scissor cover the whole render target.
		//
		// nvrhi::Viewport's six-argument constructor is (minX, maxX, minY, maxY,
		// minZ, maxZ) - X before Y. Passing (0, 0, width, height, ...) instead
		// leaves the viewport zero-width, and every draw is then clipped away: a
		// completely black frame, with nothing reported as wrong.
		//
		// The size comes from the framebuffer's own attachment, not from the
		// frame size, so a draw into a smaller target (a reflection probe face)
		// covers its target exactly.
		float viewportWidth = m_FrameWidth;
		float viewportHeight = m_FrameHeight;
		{
			const auto& fbAttachments = framebuffer->getDesc().colorAttachments;
			if (!fbAttachments.empty() && fbAttachments[0].texture)
			{
				viewportWidth = static_cast<float>(fbAttachments[0].texture->getDesc().width);
				viewportHeight = static_cast<float>(fbAttachments[0].texture->getDesc().height);
			}
		}
		nvrhi::Viewport viewport(viewportWidth, viewportHeight);
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

	// --- Real-time reflection probe ---

	bool PbrRenderer::EnableReflectionProbe(uint32_t size)
	{
		if (m_ReflectionProbeEnabled)
			return true;

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device || size == 0)
		{
			FG_CORE_ERROR("PbrRenderer: reflection probe needs a device and a non-zero size");
			return false;
		}

		// Renderable + sampleable cube, same colour format as the HDR target so
		// the one PBR pipeline (baked for m_ColorFormat/D32) is valid on both.
		nvrhi::TextureDesc cubeDesc;
		cubeDesc.width = size;
		cubeDesc.height = size;
		cubeDesc.format = m_ColorFormat;
		cubeDesc.dimension = nvrhi::TextureDimension::TextureCube;
		cubeDesc.arraySize = 6;
		cubeDesc.mipLevels = 1;
		cubeDesc.isRenderTarget = true;
		cubeDesc.isShaderResource = true;
		cubeDesc.keepInitialState = true;
		cubeDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		cubeDesc.debugName = "PbrRenderer.ReflectionProbe";
		m_ReflectionMap = device->createTexture(cubeDesc);
		if (!m_ReflectionMap)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create the reflection probe cubemap");
			return false;
		}

		// Depth shared by all six faces (re-cleared per face), same format the
		// pipeline's framebuffer info declares.
		nvrhi::TextureDesc depthDesc;
		depthDesc.width = size;
		depthDesc.height = size;
		depthDesc.format = nvrhi::Format::D32;
		depthDesc.isRenderTarget = true;
		depthDesc.keepInitialState = true;
		depthDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		depthDesc.debugName = "PbrRenderer.ReflectionProbeDepth";
		m_ReflectionDepth = device->createTexture(depthDesc);
		if (!m_ReflectionDepth)
		{
			FG_CORE_ERROR("PbrRenderer: failed to create the reflection probe depth buffer");
			m_ReflectionMap = nullptr;
			return false;
		}

		m_ReflectionFramebuffers.clear();
		m_ReflectionFramebuffers.reserve(6);
		for (uint32_t face = 0; face < 6; ++face)
		{
			nvrhi::FramebufferDesc fbDesc;
			fbDesc.addColorAttachment(m_ReflectionMap.Get(),
				nvrhi::TextureSubresourceSet(0, 1, face, 1));
			fbDesc.setDepthAttachment(m_ReflectionDepth.Get());

			nvrhi::FramebufferHandle fb = device->createFramebuffer(fbDesc);
			if (!fb)
			{
				FG_CORE_ERROR("PbrRenderer: failed to create reflection probe framebuffer for face {0}", face);
				m_ReflectionFramebuffers.clear();
				m_ReflectionMap = nullptr;
				m_ReflectionDepth = nullptr;
				return false;
			}
			m_ReflectionFramebuffers.push_back(fb);
		}

		m_ReflectionSize = size;
		m_ReflectionProbeEnabled = true;
		FG_CORE_INFO("Reflection probe created: {0}x{0} cubemap", size);
		return true;
	}

	void PbrRenderer::RenderReflectionProbe(nvrhi::ICommandList* commandList,
		const glm::mat4& shadowViewProjection,
		const glm::vec3& lightDirection,
		const glm::vec3& lightColor,
		float lightIntensity,
		const glm::vec3& ambientLight,
		const std::vector<ProbeDraw>& draws)
	{
		if (!commandList || !m_ReflectionProbeEnabled || draws.empty())
			return;

		for (uint32_t face = 0; face < 6; ++face)
		{
			const nvrhi::FramebufferHandle& fb = m_ReflectionFramebuffers[face];
			if (!fb)
				continue;

			// Clear the face and depth, mirroring how UpdateFrame clears the
			// HDR target before the main pass.
			commandList->clearTextureFloat(m_ReflectionMap,
				nvrhi::TextureSubresourceSet(0, 1, face, 1),
				nvrhi::Color(0.05f, 0.06f, 0.09f, 1.0f));
			commandList->clearDepthStencilTexture(m_ReflectionDepth,
				nvrhi::TextureSubresourceSet(0, 1, 0, 1), true, 1.0f, false, 0);

			// This face's camera. The main pass rewrites the buffer with the
			// real camera in UpdateFrame after the probe, so sharing it is safe.
			CameraUniforms uniforms{};
			uniforms.ViewProjection = BuildCubeFaceViewProjection(face, m_ReflectionProbePosition, 0.1f, 120.0f);
			uniforms.ShadowViewProjection = shadowViewProjection;
			uniforms.LightDirection = glm::vec4(lightDirection, 0.0f);
			uniforms.LightColor = glm::vec4(lightColor, lightIntensity);
			uniforms.CameraPosition = glm::vec4(m_ReflectionProbePosition, 0.0f);
			uniforms.AmbientLight = glm::vec4(ambientLight, 0.0f);
			uniforms.SSAOParams = glm::vec4(0.002f, 1.0f, 0.0f, 0.0f);
			commandList->writeBuffer(m_CameraBuffer, &uniforms, sizeof(uniforms), 0);

			for (const ProbeDraw& draw : draws)
			{
				DrawObject(commandList, fb, draw.MeshHandle, draw.AlbedoTexture,
					draw.Model, draw.NormalMatrix, draw.Albedo,
					draw.Metallic, draw.Roughness, draw.AO,
					draw.Emissive, draw.EmissiveIntensity,
					/*fromReflectionProbe*/ true);
			}
		}
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
		shadowLayoutDesc.setBindingOffsets(nvrhi::VulkanBindingOffsets{ 0, 0, 0, 0 });
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
		if (!m_ShadowSampler.Get())
		{
			FG_CORE_WARN("PbrRenderer: failed to create shadow sampler");
			return false;
		}

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

		// See DrawObject: the six-argument nvrhi::Viewport constructor takes
		// (minX, maxX, minY, maxY, minZ, maxZ), not (minX, minY, width, height).
		nvrhi::Viewport viewport(static_cast<float>(m_ShadowMapWidth), static_cast<float>(m_ShadowMapHeight));
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
		// No Release() needed: cmd's destructor will release the handle at
		// the end of this scope, and the lifetime tracker holds a separate
		// shared_ptr that keeps the TrackedCommandBuffer alive until the GPU
		// is done.

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

		// IBL layout: binding 0 = env map texture, binding 1 = its sampler,
		// binding 2 = params uniform buffer.
		nvrhi::BindingLayoutDesc iblLayoutDesc;
		iblLayoutDesc.visibility = nvrhi::ShaderType::All;
		iblLayoutDesc.setBindingOffsets(nvrhi::VulkanBindingOffsets{ 0, 0, 0, 0 });
		iblLayoutDesc.bindings = {
			{ 0, nvrhi::ResourceType::Texture_SRV, 0, 1 },
			{ 1, nvrhi::ResourceType::Sampler, 0, 1 },
			{ 2, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
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

		// Create IBL binding set (env map + sampler + params).
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
		iblSetDesc.bindings.push_back(MakeSamplerItem(m_LinearSampler.Get(), 1));
		{
			nvrhi::BindingSetItem bufItem{};
			bufItem.resourceHandle = m_IBLParamsBuffer.Get();
			bufItem.slot = 2;
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

			nvrhi::Viewport viewport(static_cast<float>(size), static_cast<float>(size));
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
		iblSetDesc.bindings.push_back(MakeSamplerItem(m_LinearSampler.Get(), 1));
		{
			nvrhi::BindingSetItem bufItem{};
			bufItem.resourceHandle = m_IBLParamsBuffer.Get();
			bufItem.slot = 2;
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

				nvrhi::Viewport viewport(
					static_cast<float>(baseSize >> mip),
					static_cast<float>(baseSize >> mip));

				nvrhi::GraphicsState state;
				state.framebuffer = mipFb.Get();
				state.viewport.addViewportAndScissorRect(viewport);
				state.pipeline = m_PrefilterPipeline.Get();
				state.bindings.push_back(iblSet.Get());
				state.vertexBuffers.push_back(m_IBLQuadVertexBinding);

				cmd->setGraphicsState(state);
				cmd->draw(nvrhi::DrawArguments{ 4, 1 });

				// No Release(): mipFb is owned by its RefCountPtr. ->Release()
				// on top of the destructor is a double release, and the
				// lifetime tracker keeps using the object after it dies.
			}
		}

		cmd->close();
		device->executeCommandList(cmd, nvrhi::CommandQueue::Graphics);

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
