#include "fgpch.h"
#include "Platform/Vulkan/VkRenderer.h"
#include "Platform/Vulkan/VkRendererInternal.h"

#include "Platform/Vulkan/Frame/Barriers.h"
#include "Platform/Vulkan/Memory/UploadRegions.h"
#include "Platform/Vulkan/Resources/Subresources.h"
#include "Platform/Vulkan/VkMesh.h"
#include "Platform/Vulkan/VkTexture.h"

#include "Forge/Core/Log.h"
#include "Forge/Renderer/Bloom.h"
#include "Forge/Renderer/Camera.h"
#include "Forge/Renderer/CpuTexture.h"
#include "Forge/Renderer/Mesh.h"
#include "Forge/Renderer/Texture.h"

#include <vk_mem_alloc.h>

#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>

namespace Forge { namespace Vulkan {

namespace {

	// Uniform block payloads, mirroring resources/shaders byte for byte
	// (GL_EXT_scalar_block_layout).
	struct CameraData
	{
		glm::mat4 ViewProjection;
		glm::mat4 ShadowViewProjection;
		glm::vec4 LightDirection;
		glm::vec4 LightColor;
		glm::vec4 CameraPosition;
		glm::vec4 AmbientLight;
		glm::vec4 SSAOParams;
	};

	struct ObjectData
	{
		glm::mat4 Transform;
		glm::mat4 NormalMatrix;
		glm::vec4 Albedo;
		glm::vec4 MetallicRoughnessAO;
		glm::vec4 Emissive;
	};

	struct ShadowMapParams
	{
		glm::vec2 ShadowMapSize;
		glm::vec2 ShadowBias;   // x = normal offset (world), y = depth bias (NDC)
	};

	struct SsaoParams
	{
		glm::mat4 InvViewProjection;
		float Intensity;
		float Radius;
		float Bias;
		float Power;
		glm::vec2 TexelSize;
	};

	struct GpuBloomParams
	{
		glm::vec2 TexelSize;   // 1 / destination (half-res) dimensions
		float Threshold;
		float Knee;
		float Scale;
	};

	struct GpuBlurParams
	{
		glm::vec2 TexelSize;
		float Radius;
	};

	struct GpuHdrParams
	{
		float Exposure;
		float BloomIntensity;
	};

	struct GpuFinalParams
	{
		glm::vec4 SsaIntensity;   // shader reads the x component
	};

	struct GpuIrradianceParams
	{
		glm::vec4 NormalAndRadius;   // xyz = face normal, w = radius
	};

	struct GpuPrefilterParams
	{
		glm::vec4 ReflectionAndRoughness;   // xyz = R, w = roughness
	};

	// The payloads must match the shader blocks byte for byte, or the shader
	// reads the wrong members.
	static_assert(sizeof(CameraData) == 208, "CameraData must match pbr.frag CameraData (2*mat4 + 5*vec4)");
	static_assert(sizeof(ObjectData) == 176, "ObjectData must match ObjectData in pbr.vert/pbr.frag (2*mat4 + 3*vec4)");
	static_assert(sizeof(ShadowMapParams) == 16, "ShadowMapParams must match ShadowMapParams in pbr.frag (2*vec2)");
	static_assert(sizeof(SsaoParams) == 88, "SsaoParams must match SSAOParams in ssao.frag (mat4 + 4*float + vec2)");
	static_assert(sizeof(GpuBloomParams) == 20, "GpuBloomParams must match BloomParams in bloom.frag (vec2 + 3*float)");
	static_assert(sizeof(GpuBlurParams) == 12, "GpuBlurParams must match BlurParams in blur.frag (vec2 + float)");
	static_assert(sizeof(GpuHdrParams) == 8, "GpuHdrParams must match HDRParams in hdr.frag (2*float)");
	static_assert(sizeof(GpuIrradianceParams) == 16, "IrradianceParams must match IrradianceParams in irradiance.frag (vec3 + float)");
	static_assert(sizeof(GpuPrefilterParams) == 16, "PrefilterParams must match PrefilterParams in prefilter.frag (vec3 + float)");

	// IEEE binary16: the HDR upload converts CPU float radiance to RGBA16F.
	// Round-to-nearest-even; saturates to infinity past half's exponent range.
	[[nodiscard]] uint16_t FloatToHalf(float value)
	{
		const uint32_t bits = std::bit_cast<uint32_t>(value);
		const uint32_t sign = (bits >> 16U) & 0x8000U;
		const int32_t exponent = static_cast<int32_t>((bits >> 23U) & 0xFFU) - 127 + 15;
		const uint32_t mantissa = bits & 0x7FFFFFU;

		if (exponent <= 0)
		{
			if (exponent < -10)
				return static_cast<uint16_t>(sign);   // zero / subnormal flush
			uint32_t m = mantissa | 0x800000U;        // implicit leading 1
			const int32_t shift = 14 - exponent;
			uint32_t half = m >> static_cast<uint32_t>(shift);
			const uint32_t rest = m & ((1U << static_cast<uint32_t>(shift)) - 1U);
			const uint32_t halfUnit = 1U << static_cast<uint32_t>(shift - 1);
			if (rest > halfUnit || (rest == halfUnit && (half & 1U)))
				++half;
			return static_cast<uint16_t>(sign | half);
		}
		if (exponent >= 31)
			return static_cast<uint16_t>(sign | (0x1FU << 10) | (mantissa ? 0x200U : 0U));

		uint16_t half = static_cast<uint16_t>(sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
		const uint32_t rest = mantissa & 0x1FFFU;
		if (rest > 0x1000U || (rest == 0x1000U && (half & 1U)))
			++half;   // carries into the exponent on overflow: infinity
		return half;
	}

	constexpr glm::vec4 kClearColor{ 0.05f, 0.06f, 0.09f, 1.0f };

	void SetViewportScissor(const Device& device, ::VkCommandBuffer cmd, uint32_t width, uint32_t height)
	{
		const VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
		device.GetFunctions().CmdSetViewport(cmd, 0, 1, &viewport);
		const VkRect2D scissor{ { 0, 0 }, { width, height } };
		device.GetFunctions().CmdSetScissor(cmd, 0, 1, &scissor);
	}

	// Begin a render pass with its clears. The clear values are supplied in
	// attachment order: colour attachments first, depth last.
	void BeginRenderPass(const Device& device, ::VkCommandBuffer cmd, ::VkRenderPass renderPass,
		::VkFramebuffer framebuffer, uint32_t width, uint32_t height,
		std::span<const VkClearValue> clears)
	{
		VkRenderPassBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		begin.renderPass = renderPass;
		begin.framebuffer = framebuffer;
		begin.renderArea = { { 0, 0 }, { width, height } };
		begin.clearValueCount = static_cast<uint32_t>(clears.size());
		begin.pClearValues = clears.data();
		device.GetFunctions().CmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
	}

	// The 4-vertex fullscreen quad, drawn as a triangle strip, unindexed.
	void DrawFullscreenQuad(const Device& device, ::VkCommandBuffer cmd, ::VkPipeline pipeline,
		::VkPipelineLayout layout, ::VkDescriptorSet set, uint32_t width, uint32_t height)
	{
		device.GetFunctions().CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		device.GetFunctions().CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
		SetViewportScissor(device, cmd, width, height);
		device.GetFunctions().CmdDraw(cmd, 4, 1, 0, 0);
	}

	[[nodiscard]] VkDescriptorBufferInfo BufferRange(::VkBuffer buffer, size_t offset, size_t size)
	{
		return VkDescriptorBufferInfo{ buffer, static_cast<VkDeviceSize>(offset), static_cast<VkDeviceSize>(size) };
	}

	[[nodiscard]] VkDescriptorImageInfo ImageBinding(::VkSampler sampler, ::VkImageView view)
	{
		return VkDescriptorImageInfo{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	}

}  // namespace

// ---------------------------------------------------------------------------
// Uniform writes
// ---------------------------------------------------------------------------

void VkRenderer::WriteCameraUniforms(const CameraParams& camera)
{
	if (!m_CameraUboData)
		return;
	CameraData data{};
	data.ViewProjection = camera.ViewProjection;
	data.ShadowViewProjection = camera.ShadowViewProjection;
	data.LightDirection = glm::vec4(camera.LightDirection, 0.0f);
	data.LightColor = glm::vec4(camera.LightColor, camera.LightIntensity);
	data.CameraPosition = glm::vec4(camera.CameraPosition, 0.0f);
	data.AmbientLight = glm::vec4(camera.AmbientLight, 0.0f);
	// x is not read: the shadow bias travels in ShadowMapParams, where the
	// texel size it has to be scaled by lives.
	data.SSAOParams = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
	std::memcpy(m_CameraUboData, &data, sizeof(data));
}

void VkRenderer::WriteShadowParams(const CameraParams& camera)
{
	if (!m_ShadowParamsUboData)
		return;
	ShadowMapParams params{};
	params.ShadowMapSize = glm::vec2(static_cast<float>(m_ShadowMapSize), static_cast<float>(m_ShadowMapSize));
	params.ShadowBias = camera.ShadowBias;
	std::memcpy(m_ShadowParamsUboData, &params, sizeof(params));
}

void VkRenderer::EndActiveRenderPass()
{
	if (!m_RenderPassOpen)
		return;
	const ::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd != VK_NULL_HANDLE)
		m_Device.GetFunctions().CmdEndRenderPass(cmd);
	m_RenderPassOpen = false;
}

// ---------------------------------------------------------------------------
// Scene pass
// ---------------------------------------------------------------------------

void VkRenderer::BeginScene(const CameraParams& camera)
{
	if (!m_DeviceReady || !HasTargets() || m_PipelinePbr == VK_NULL_HANDLE)
		return;

	m_FrameCamera = camera;
	m_CurrentTargetWidth = m_RenderWidth;
	m_CurrentTargetHeight = m_RenderHeight;

	WriteCameraUniforms(camera);
	WriteShadowParams(camera);

	EndActiveRenderPass();
	const std::array<VkClearValue, 2> clears{
		VkClearValue{ .color = { kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3] } },
		VkClearValue{ .depthStencil = { 1.0f, 0 } },
	};
	BeginRenderPass(m_Device, CurrentCommandBuffer(), m_RenderPassHdr, m_FbHdr,
		m_RenderWidth, m_RenderHeight, clears);
	m_RenderPassOpen = true;
	m_SceneBegun = true;
}

void VkRenderer::DrawObject(const Ref<Mesh>& mesh, const Ref<Texture>& albedoTexture,
	const ObjectParams& object, bool probeDraw)
{
	if (!m_DeviceReady || !m_RenderPassOpen || m_PipelinePbr == VK_NULL_HANDLE)
		return;

	MeshGpu* gpu = GetMeshGpu(mesh);
	if (!gpu)
		return;

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;

	// One ring slot per draw: the descriptor's range points the shader's
	// ObjectData at this draw's copy.
	const size_t slotOffset = static_cast<size_t>(m_ObjectSlot) * sizeof(ObjectData);
	m_ObjectSlot = (m_ObjectSlot + 1U) % ObjectRingSize;
	ObjectData data{};
	data.Transform = object.Model;
	data.NormalMatrix = glm::mat4(object.NormalMatrix);
	data.Albedo = glm::vec4(object.Albedo, 1.0f);
	data.MetallicRoughnessAO = glm::vec4(object.Metallic, object.Roughness, object.AO, 0.0f);
	data.Emissive = glm::vec4(object.Emissive, object.EmissiveIntensity);
	std::memcpy(static_cast<uint8_t*>(m_ObjectUboData) + slotOffset, &data, sizeof(data));

	const TextureGpu* albedo = GetTextureGpu(albedoTexture);
	::VkDescriptorSet set = BuildPbrSet(m_ObjectUbo, slotOffset,
		albedo ? albedo->View : m_WhiteView, probeDraw);
	if (set == VK_NULL_HANDLE)
		return;

	const DeviceFunctions& functions = m_Device.GetFunctions();
	functions.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelinePbr);
	functions.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LayoutPbr, 0, 1, &set, 0, nullptr);
	const ::VkDeviceSize offset = 0;
	functions.CmdBindVertexBuffers(cmd, 0, 1, &gpu->VertexBuffer, &offset);
	functions.CmdBindIndexBuffer(cmd, gpu->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
	SetViewportScissor(m_Device, cmd, m_CurrentTargetWidth, m_CurrentTargetHeight);
	functions.CmdDrawIndexed(cmd, gpu->IndexCount, 1, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Shadow pass
// ---------------------------------------------------------------------------

void VkRenderer::BeginShadowScene(const glm::mat4& shadowViewProjection)
{
	if (!m_DeviceReady || !HasTargets() || m_PipelineShadow == VK_NULL_HANDLE)
		return;

	if (m_ShadowCameraUboData)
		std::memcpy(m_ShadowCameraUboData, &shadowViewProjection, sizeof(glm::mat4));

	m_CurrentTargetWidth = m_ShadowMapSize;
	m_CurrentTargetHeight = m_ShadowMapSize;

	EndActiveRenderPass();
	const std::array<VkClearValue, 1> clears{
		VkClearValue{ .depthStencil = { 1.0f, 0 } },
	};
	BeginRenderPass(m_Device, CurrentCommandBuffer(), m_RenderPassShadow, m_FbShadow,
		m_ShadowMapSize, m_ShadowMapSize, clears);
	m_RenderPassOpen = true;
}

void VkRenderer::DrawShadowObject(const Ref<Mesh>& mesh, const glm::mat4& model)
{
	if (!m_DeviceReady || !m_RenderPassOpen || m_PipelineShadow == VK_NULL_HANDLE)
		return;

	MeshGpu* gpu = GetMeshGpu(mesh);
	if (!gpu)
		return;

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;

	const size_t slotOffset = static_cast<size_t>(m_ObjectSlot) * sizeof(ObjectData);
	m_ObjectSlot = (m_ObjectSlot + 1U) % ObjectRingSize;
	ObjectData data{};
	data.Transform = model;
	data.NormalMatrix = glm::mat4(glm::mat3(1.0f));
	data.Albedo = glm::vec4(1.0f);
	data.MetallicRoughnessAO = glm::vec4(0.0f, 0.5f, 1.0f, 0.0f);
	data.Emissive = glm::vec4(0.0f);
	std::memcpy(static_cast<uint8_t*>(m_ObjectUboData) + slotOffset, &data, sizeof(data));

	::VkDescriptorSet set = BuildShadowSet(m_ObjectUbo, slotOffset);
	if (set == VK_NULL_HANDLE)
		return;

	const DeviceFunctions& functions = m_Device.GetFunctions();
	functions.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineShadow);
	functions.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_LayoutShadow, 0, 1, &set, 0, nullptr);
	const ::VkDeviceSize offset = 0;
	functions.CmdBindVertexBuffers(cmd, 0, 1, &gpu->VertexBuffer, &offset);
	functions.CmdBindIndexBuffer(cmd, gpu->IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
	SetViewportScissor(m_Device, cmd, m_ShadowMapSize, m_ShadowMapSize);
	functions.CmdDrawIndexed(cmd, gpu->IndexCount, 1, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Post passes (engine order: SSAO -> Bloom -> Tonemap -> Final)
// ---------------------------------------------------------------------------

void VkRenderer::RenderSSAOPass(const glm::mat4& viewProjection)
{
	EndActiveRenderPass();
	if (!HasTargets() || m_PipelineSsao == VK_NULL_HANDLE)
		return;

	SsaoParams params{};
	params.InvViewProjection = glm::inverse(viewProjection);
	params.Intensity = 1.0f;
	params.Radius = 0.5f;
	params.Bias = 0.002f;
	params.Power = 1.5f;
	params.TexelSize = glm::vec2(1.0f / static_cast<float>(m_RenderWidth), 1.0f / static_cast<float>(m_RenderHeight));
	std::memcpy(m_SsaoUboData, &params, sizeof(params));

	const std::array<::VkImageView, 1> views{ m_DepthView };
	const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
	const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_SsaoUbo, 0, sizeof(SsaoParams)) };
	::VkDescriptorSet set = BuildPostSet(m_SetLayoutSsao, views, samplers, buffers);
	if (set == VK_NULL_HANDLE)
		return;

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
	BeginRenderPass(m_Device, cmd, m_RenderPassR8, m_FbSsao, m_RenderWidth, m_RenderHeight, clears);
	DrawFullscreenQuad(m_Device, cmd, m_PipelineSsao, m_LayoutSsao, set, m_RenderWidth, m_RenderHeight);
	m_Device.GetFunctions().CmdEndRenderPass(cmd);
}

void VkRenderer::RenderBloomPass()
{
	EndActiveRenderPass();
	if (!HasTargets() || m_PipelineBloom == VK_NULL_HANDLE || m_PipelineBlur == VK_NULL_HANDLE)
		return;

	const uint32_t halfWidth = std::max(1u, m_RenderWidth / 2u);
	const uint32_t halfHeight = std::max(1u, m_RenderHeight / 2u);

	const BloomParams defaults{};
	GpuBloomParams bloomParams{};
	bloomParams.TexelSize = glm::vec2(1.0f / static_cast<float>(halfWidth), 1.0f / static_cast<float>(halfHeight));
	bloomParams.Threshold = defaults.Threshold;
	bloomParams.Knee = defaults.Knee;
	bloomParams.Scale = defaults.Scale;
	std::memcpy(m_BloomUboData, &bloomParams, sizeof(bloomParams));

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;

	// Step 1: soft-threshold luminance + 13-tap downsample, full-res HDR -> half-res.
	{
		const std::array<::VkImageView, 1> views{ m_HdrView };
		const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
		const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_BloomUbo, 0, sizeof(GpuBloomParams)) };
		::VkDescriptorSet set = BuildPostSet(m_SetLayoutBloom, views, samplers, buffers);
		if (set == VK_NULL_HANDLE)
			return;
		const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
		BeginRenderPass(m_Device, cmd, m_RenderPassColor16, m_FbBloomHalf, halfWidth, halfHeight, clears);
		DrawFullscreenQuad(m_Device, cmd, m_PipelineBloom, m_LayoutBloom, set, halfWidth, halfHeight);
		m_Device.GetFunctions().CmdEndRenderPass(cmd);
	}

	// Step 2: three 16-tap golden-spiral blurs, ping-ponging Half -> A -> B -> A
	// with growing radius; the final blur leaves the result in BloomA, which
	// the tonemap pass binds.
	const ::VkFramebuffer targets[3] = { m_FbBloomA, m_FbBloomB, m_FbBloomA };
	const ::VkImageView sources[3] = { m_BloomHalfView, m_BloomAView, m_BloomBView };

	for (int iteration = 0; iteration < 3; ++iteration)
	{
		GpuBlurParams blurParams{};
		blurParams.TexelSize = glm::vec2(1.0f / static_cast<float>(halfWidth), 1.0f / static_cast<float>(halfHeight));
		blurParams.Radius = 1.0f + static_cast<float>(iteration);   // widening disc per iteration
		std::memcpy(m_BlurUboData, &blurParams, sizeof(blurParams));

		const std::array<::VkImageView, 1> views{ sources[iteration] };
		const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
		const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_BlurUbo, 0, sizeof(GpuBlurParams)) };
		::VkDescriptorSet set = BuildPostSet(m_SetLayoutBloom, views, samplers, buffers);
		if (set == VK_NULL_HANDLE)
			return;

		const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
		BeginRenderPass(m_Device, cmd, m_RenderPassColor16, targets[iteration], halfWidth, halfHeight, clears);
		DrawFullscreenQuad(m_Device, cmd, m_PipelineBlur, m_LayoutBloom, set, halfWidth, halfHeight);
		m_Device.GetFunctions().CmdEndRenderPass(cmd);
	}
}

void VkRenderer::RenderTonemapPass()
{
	EndActiveRenderPass();
	if (!HasTargets() || m_PipelineTonemap == VK_NULL_HANDLE)
		return;

	// HDR uniform: exposure + bloom composite weight.
	GpuHdrParams hdrParams{ 1.0f, s_BloomIntensity };
	std::memcpy(m_HdrUboData, &hdrParams, sizeof(hdrParams));

	const std::array<::VkImageView, 2> views{ m_HdrView, m_BloomAView };
	const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
	const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_HdrUbo, 0, sizeof(GpuHdrParams)) };
	::VkDescriptorSet set = BuildPostSet(m_SetLayoutTonemap, views, samplers, buffers);
	if (set == VK_NULL_HANDLE)
		return;

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;

	const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
	BeginRenderPass(m_Device, cmd, m_RenderPassColor16, m_FbTonemap, m_PresentWidth, m_PresentHeight, clears);
	DrawFullscreenQuad(m_Device, cmd, m_PipelineTonemap, m_LayoutTonemap, set, m_PresentWidth, m_PresentHeight);
	m_Device.GetFunctions().CmdEndRenderPass(cmd);
}

void VkRenderer::RenderFinalPass()
{
	EndActiveRenderPass();
	if (!HasTargets() || m_PipelineFinal == VK_NULL_HANDLE || m_CurrentImageIndex == UINT32_MAX)
		return;
	if (m_CurrentImageIndex >= m_SwapchainFramebuffers.size())
		return;

	GpuFinalParams finalParams{ glm::vec4(0.3f, 0.0f, 0.0f, 0.0f) };   // SSAO intensity
	std::memcpy(m_FinalUboData, &finalParams, sizeof(finalParams));

	const std::array<::VkImageView, 2> views{ m_TonemapView, m_SsaoView };
	const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
	const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_FinalUbo, 0, sizeof(GpuFinalParams)) };
	::VkDescriptorSet set = BuildPostSet(m_SetLayoutFinal, views, samplers, buffers);
	if (set == VK_NULL_HANDLE)
		return;

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;

	// The present render pass has DONT_CARE load: the whole quad covers it.
	BeginRenderPass(m_Device, cmd, m_RenderPassPresent, m_SwapchainFramebuffers[m_CurrentImageIndex],
		m_PresentWidth, m_PresentHeight, {});
	DrawFullscreenQuad(m_Device, cmd, m_PipelineFinal, m_LayoutFinal, set, m_PresentWidth, m_PresentHeight);
	m_Device.GetFunctions().CmdEndRenderPass(cmd);
}

// ---------------------------------------------------------------------------
// Descriptor set builders
// ---------------------------------------------------------------------------

::VkDescriptorSet VkRenderer::BuildPbrSet(::VkBuffer objectBuffer, size_t objectOffset,
	::VkImageView albedoView, bool probeDraw)
{
	std::vector<::VkDescriptorSet> sets;
	std::string reason;
	if (!m_DescriptorPool.AllocateSets(m_SetLayoutPbr, 1, sets, reason))
	{
		FG_CORE_ERROR("VkRenderer: PBR descriptor set unavailable ({0})", reason);
		return VK_NULL_HANDLE;
	}

	const ::VkDescriptorSet set = sets[0];

	// The probe pass writes the reflection cubemap, so it binds the static
	// fallback environment: a draw that samples the image it is writing is
	// feedback, not a reflection.
	const ::VkImageView envView = (probeDraw || m_ReflectionView == VK_NULL_HANDLE)
		? m_EnvFallbackView : m_ReflectionView;
	const ::VkImageView irradianceView = m_IrradianceView != VK_NULL_HANDLE ? m_IrradianceView : m_IrradianceFallbackView;
	const ::VkImageView albedo = albedoView != VK_NULL_HANDLE ? albedoView : m_WhiteView;

	const std::array<VkDescriptorBufferInfo, 3> buffers{
		BufferRange(m_CameraUbo, 0, sizeof(CameraData)),
		BufferRange(objectBuffer, objectOffset, sizeof(ObjectData)),
		BufferRange(m_ShadowParamsUbo, 0, sizeof(ShadowMapParams)),
	};
	const std::array<VkDescriptorImageInfo, 6> images{
		ImageBinding(m_SamplerAlbedo, albedo),
		ImageBinding(m_SamplerShadow, m_ShadowView),
		ImageBinding(m_SamplerLinear, VK_NULL_HANDLE),
		ImageBinding(m_SamplerLinear, envView),
		ImageBinding(m_SamplerLinear, irradianceView),
		ImageBinding(m_SamplerLinear, VK_NULL_HANDLE),
	};
	(void)images[2];   // binding 6: the linear sampler alone (no view)
	(void)images[5];

	const std::array<VkWriteDescriptorSet, 12> writes{
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers[0], nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers[1], nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[0], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &images[0], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[1], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 5, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &images[1], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 6, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &images[2], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 7, 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers[2], nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 8, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[3], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 9, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &images[3], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 10, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[4], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 11, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &images[4], nullptr, nullptr },
	};
	m_Device.GetFunctions().UpdateDescriptorSets(m_Device.Handle(),
		static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	return set;
}

::VkDescriptorSet VkRenderer::BuildShadowSet(::VkBuffer objectBuffer, size_t objectOffset)
{
	std::vector<::VkDescriptorSet> sets;
	std::string reason;
	if (!m_DescriptorPool.AllocateSets(m_SetLayoutShadow, 1, sets, reason))
	{
		FG_CORE_ERROR("VkRenderer: shadow descriptor set unavailable ({0})", reason);
		return VK_NULL_HANDLE;
	}

	const ::VkDescriptorSet set = sets[0];
	const std::array<VkDescriptorBufferInfo, 2> buffers{
		BufferRange(objectBuffer, objectOffset, sizeof(ObjectData)),
		BufferRange(m_ShadowCameraUbo, 0, sizeof(glm::mat4)),
	};
	const std::array<VkWriteDescriptorSet, 2> writes{
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers[0], nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers[1], nullptr },
	};
	m_Device.GetFunctions().UpdateDescriptorSets(m_Device.Handle(),
		static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	return set;
}

::VkDescriptorSet VkRenderer::BuildPostSet(const DescriptorSetLayout& layout,
	std::span<const ::VkImageView> views,
	std::span<const ::VkSampler> samplers,
	std::span<const VkDescriptorBufferInfo> buffers)
{
	std::vector<::VkDescriptorSet> sets;
	std::string reason;
	if (!m_DescriptorPool.AllocateSets(layout, 1, sets, reason))
	{
		FG_CORE_ERROR("VkRenderer: post descriptor set unavailable ({0})", reason);
		return VK_NULL_HANDLE;
	}

	const ::VkDescriptorSet set = sets[0];

	// Fill the layout's bindings in binding order, taking the next resource
	// of each type as its binding asks for one. A caller that runs out of a
	// type binds VK_NULL_HANDLE: "not bound" is visible as a black sample,
	// not as a half-built set that fails at submit.
	std::vector<VkWriteDescriptorSet> writes;
	std::vector<VkDescriptorImageInfo> imageInfos;
	writes.reserve(layout.GetBindings().size());
	imageInfos.reserve(layout.GetBindings().size());

	uint32_t viewIndex = 0;
	uint32_t samplerIndex = 0;
	uint32_t bufferIndex = 0;

	for (const VkDescriptorSetLayoutBinding& binding : layout.GetBindings())
	{
		switch (binding.descriptorType)
		{
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		{
			const ::VkImageView view = viewIndex < views.size() ? views[viewIndex] : VK_NULL_HANDLE;
			++viewIndex;
			imageInfos.push_back(VkDescriptorImageInfo{ VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
			writes.push_back(VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set,
				binding.binding, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &imageInfos.back(), nullptr, nullptr });
			break;
		}
		case VK_DESCRIPTOR_TYPE_SAMPLER:
		{
			const ::VkSampler sampler = samplerIndex < samplers.size() ? samplers[samplerIndex] : VK_NULL_HANDLE;
			++samplerIndex;
			imageInfos.push_back(VkDescriptorImageInfo{ sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED });
			writes.push_back(VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set,
				binding.binding, 1, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &imageInfos.back(), nullptr, nullptr });
			break;
		}
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		{
			const VkDescriptorBufferInfo buffer = bufferIndex < buffers.size() ? buffers[bufferIndex]
				: VkDescriptorBufferInfo{ VK_NULL_HANDLE, 0, 0 };
			++bufferIndex;
			writes.push_back(VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set,
				binding.binding, 1, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffer, nullptr });
			break;
		}
		default:
			break;
		}
	}

	m_Device.GetFunctions().UpdateDescriptorSets(m_Device.Handle(),
		static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	return set;
}

// ---------------------------------------------------------------------------
// Reflection probe
// ---------------------------------------------------------------------------

bool VkRenderer::EnableReflectionProbe(uint32_t size)
{
	if (m_ProbeEnabled)
		return true;
	if (size == 0 || !m_DeviceReady)
	{
		FG_CORE_ERROR("VkRenderer: reflection probe needs a device and a non-zero size");
		return false;
	}
	if (!CreateProbeResources())
		return false;
	m_ProbeSize = size;
	m_ProbeEnabled = true;
	FG_CORE_INFO("VkRenderer: reflection probe created ({0}x{0} cubemap)", size);
	return true;
}

bool VkRenderer::CreateProbeResources()
{
	m_ReflectionImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
		VkExtent3D{ m_ProbeSize, m_ProbeSize, 1 }, 1, 6, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, "Probe.Color", m_ReflectionAlloc);
	m_ReflectionDepthImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
		VkExtent3D{ m_ProbeSize, m_ProbeSize, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, "Probe.Depth", m_ReflectionDepthAlloc);
	if (m_ReflectionImage == VK_NULL_HANDLE || m_ReflectionDepthImage == VK_NULL_HANDLE)
		return false;

	m_ReflectionView = CreateViewImpl(m_Device, m_ReflectionImage, VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_ASPECT_COLOR_BIT, 6, VK_IMAGE_VIEW_TYPE_CUBE);
	m_ReflectionDepthView = CreateViewImpl(m_Device, m_ReflectionDepthImage, VK_FORMAT_D32_SFLOAT,
		VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);
	if (m_ReflectionView == VK_NULL_HANDLE || m_ReflectionDepthView == VK_NULL_HANDLE)
		return false;

	// A single-layer 2D view over one cube slice: the face framebuffer's
	// colour attachment.
	auto makeFaceView = [this](::VkImage image, uint32_t layer, uint32_t mip) -> ::VkImageView
	{
		VkImageViewCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = image;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		info.subresourceRange = Subresources::Single(VK_IMAGE_ASPECT_COLOR_BIT, mip, layer);
		::VkImageView view = VK_NULL_HANDLE;
		if (m_Device.GetFunctions().CreateImageView(m_Device.Handle(), &info, nullptr, &view) != VK_SUCCESS)
			FG_CORE_ERROR("VkRenderer: cubemap face view creation failed");
		return view;
	};

	m_ReflectionFaceViews.resize(6);
	m_ReflectionFaceFramebuffers.resize(6);
	for (uint32_t face = 0; face < 6; ++face)
	{
		m_ReflectionFaceViews[face] = makeFaceView(m_ReflectionImage, face, 0);
		if (m_ReflectionFaceViews[face] == VK_NULL_HANDLE)
			return false;
		const std::array<::VkImageView, 2> views{ m_ReflectionFaceViews[face], m_ReflectionDepthView };
		m_ReflectionFaceFramebuffers[face] = CreateFramebufferImpl(m_Device, m_RenderPassHdr, views,
			m_ProbeSize, m_ProbeSize);
		if (m_ReflectionFaceFramebuffers[face] == VK_NULL_HANDLE)
			return false;
	}
	return true;
}

void VkRenderer::RenderReflectionProbe(const CameraParams& baseCamera, const std::vector<ProbeDraw>& draws)
{
	if (!m_ProbeEnabled || draws.empty() || !m_FrameLoop)
		return;

	const DeviceFunctions& functions = m_Device.GetFunctions();
	for (uint32_t face = 0; face < 6; ++face)
	{
		EndActiveRenderPass();
		::VkCommandBuffer cmd = CurrentCommandBuffer();
		if (cmd == VK_NULL_HANDLE || m_ReflectionFaceFramebuffers[face] == VK_NULL_HANDLE)
			return;

		CameraParams faceCamera = baseCamera;
		faceCamera.ViewProjection = Forge::BuildCubeFaceViewProjection(face, m_ProbePosition, 0.1f, 120.0f);
		faceCamera.CameraPosition = m_ProbePosition;
		WriteCameraUniforms(faceCamera);

		const std::array<VkClearValue, 2> clears{
			VkClearValue{ .color = { 0.05f, 0.06f, 0.09f, 1.0f } },
			VkClearValue{ .depthStencil = { 1.0f, 0 } },
		};
		BeginRenderPass(m_Device, cmd, m_RenderPassHdr, m_ReflectionFaceFramebuffers[face],
			m_ProbeSize, m_ProbeSize, clears);
		m_RenderPassOpen = true;

		m_CurrentTargetWidth = m_ProbeSize;
		m_CurrentTargetHeight = m_ProbeSize;

		for (const ProbeDraw& draw : draws)
		{
			ObjectParams object{};
			object.Model = draw.Model;
			object.NormalMatrix = draw.NormalMatrix;
			object.Albedo = draw.Albedo;
			object.Metallic = draw.Metallic;
			object.Roughness = draw.Roughness;
			object.AO = draw.AO;
			object.Emissive = draw.Emissive;
			object.EmissiveIntensity = draw.EmissiveIntensity;
			DrawObject(draw.MeshHandle, draw.AlbedoTexture, object, /*probeDraw*/ true);
		}

		functions.CmdEndRenderPass(cmd);
		m_RenderPassOpen = false;
	}

	// The main pass rewrites the camera buffer with the real camera in
	// BeginScene after the probe, so restoring it here keeps the frame's
	// post passes consistent with the last camera that drew.
	m_CurrentTargetWidth = m_RenderWidth;
	m_CurrentTargetHeight = m_RenderHeight;
	WriteCameraUniforms(m_FrameCamera);
}

// ---------------------------------------------------------------------------
// Environment map (IBL)
// ---------------------------------------------------------------------------

bool VkRenderer::LoadEnvironmentMap(const Ref<CpuTexture>& envMap)
{
	if (!m_DeviceReady || !envMap)
	{
		FG_CORE_ERROR("VkRenderer: environment map needs a device and decoded pixels");
		return false;
	}
	if (!envMap->IsHDR() || envMap->GetWidth() == 0 || envMap->GetHeight() == 0)
	{
		FG_CORE_ERROR("VkRenderer: environment map must be an HDR image with pixels");
		return false;
	}

	const uint32_t width = envMap->GetWidth();
	const uint32_t height = envMap->GetHeight();
	if (!CreateIblResources(width, height))
		return false;

	// Upload the equirect map: CPU float RGB -> RGBA16F.
	{
		const std::vector<float>& floats = envMap->GetFloats();
		const size_t pixels = static_cast<size_t>(width) * height;
		std::vector<uint16_t> texels(pixels * 4);
		for (size_t i = 0; i < pixels; ++i)
		{
			texels[i * 4 + 0] = FloatToHalf(floats[i * 3 + 0]);
			texels[i * 4 + 1] = FloatToHalf(floats[i * 3 + 1]);
			texels[i * 4 + 2] = FloatToHalf(floats[i * 3 + 2]);
			texels[i * 4 + 3] = FloatToHalf(1.0f);
		}

		Ref<Buffer> staging = m_Staging->AddStagingBuffer(texels.data(), texels.size() * sizeof(uint16_t));
		if (!staging)
			return false;
		::VkCommandBuffer cmd = m_Staging->NextCommandBuffer();
		if (cmd == VK_NULL_HANDLE)
			return false;

		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		m_Device.GetFunctions().BeginCommandBuffer(cmd, &begin);

		BufferBarrierBuilder bufferBarrier{};
		bufferBarrier.SrcAccess = VK_ACCESS_HOST_WRITE_BIT;
		bufferBarrier.DstAccess = VK_ACCESS_TRANSFER_READ_BIT;
		bufferBarrier.SrcStages = VK_PIPELINE_STAGE_HOST_BIT;
		bufferBarrier.DstStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
		RecordBufferBarrier(m_Device, cmd, bufferBarrier, staging->Handle());

		ImageBarrierBuilder imageBarrier{};
		imageBarrier.SubresourceRange = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
		imageBarrier.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrier.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.DstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
		RecordUploadPreBarriers(m_Device, cmd, staging->Handle(), m_EnvImage, imageBarrier);

		VkBufferImageCopy copy{};
		copy.imageSubresource = Subresources::FullLayers(VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1);
		copy.imageExtent = VkExtent3D{ width, height, 1 };
		m_Device.GetFunctions().CmdCopyBufferToImage(cmd, staging->Handle(), m_EnvImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

		imageBarrier.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageBarrier.SrcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
		imageBarrier.DstAccess = VK_ACCESS_SHADER_READ_BIT;
		imageBarrier.SrcStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
		imageBarrier.DstStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		RecordUploadPostBarrier(m_Device, cmd, m_EnvImage, imageBarrier);

		m_Device.GetFunctions().EndCommandBuffer(cmd);
		if (!m_Staging->Submit() || !m_Staging->Wait())
			return false;
	}

	// Irradiance: convolve the equirect map into the 64x64 cube, face by face.
	{
		static const glm::vec3 faceNormals[6] = {
			{ 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
		};
		for (uint32_t face = 0; face < 6; ++face)
		{
			GpuIrradianceParams params{ glm::vec4(faceNormals[face], 1.0f) };
			std::memcpy(m_IrradianceUboData, &params, sizeof(params));

			const std::array<::VkImageView, 1> views{ m_EnvImageView };
			const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
			const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_IrradianceUbo, 0, sizeof(GpuIrradianceParams)) };
			::VkDescriptorSet set = BuildPostSet(m_SetLayoutIrradiance, views, samplers, buffers);
			if (set == VK_NULL_HANDLE)
				return false;

			::VkCommandBuffer cmd = CurrentCommandBuffer();
			if (cmd == VK_NULL_HANDLE)
				return false;
			const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
			BeginRenderPass(m_Device, cmd, m_RenderPassCubeFace, m_IrradianceFaceFramebuffers[face],
				m_IrradianceSize, m_IrradianceSize, clears);
			DrawFullscreenQuad(m_Device, cmd, m_PipelineIrradiance, m_LayoutIrradiance, set,
				m_IrradianceSize, m_IrradianceSize);
			m_Device.GetFunctions().CmdEndRenderPass(cmd);
		}
	}

	// Prefilter: importance-sampled GGX into the mip chain, roughness per mip.
	{
		for (uint32_t mip = 0; mip < m_PrefilterMipCount; ++mip)
		{
			const float roughness = m_PrefilterMipCount > 1
				? static_cast<float>(mip) / static_cast<float>(m_PrefilterMipCount - 1)
				: 0.0f;
			GpuPrefilterParams params{ glm::vec4(0.0f, 0.0f, 1.0f, roughness) };
			std::memcpy(m_PrefilterUboData, &params, sizeof(params));

			const uint32_t mipSize = std::max(1u, m_PrefilterSize >> mip);
			for (uint32_t face = 0; face < 6; ++face)
			{
				const std::array<::VkImageView, 1> views{ m_EnvImageView };
				const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
				const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_PrefilterUbo, 0, sizeof(GpuPrefilterParams)) };
				::VkDescriptorSet set = BuildPostSet(m_SetLayoutPrefilter, views, samplers, buffers);
				if (set == VK_NULL_HANDLE)
					return false;

				::VkCommandBuffer cmd = CurrentCommandBuffer();
				if (cmd == VK_NULL_HANDLE)
					return false;
				const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
				BeginRenderPass(m_Device, cmd, m_RenderPassCubeFace,
					m_PrefilterFaceFramebuffers[mip * 6 + face], mipSize, mipSize, clears);
				DrawFullscreenQuad(m_Device, cmd, m_PipelinePrefilter, m_LayoutPrefilter, set, mipSize, mipSize);
				m_Device.GetFunctions().CmdEndRenderPass(cmd);
			}
		}
	}

	FG_CORE_INFO("VkRenderer: environment map {0}x{1}; irradiance {2}, prefilter {3} mips",
		width, height, m_IrradianceSize, m_PrefilterMipCount);
	return true;
}

bool VkRenderer::CreateIblResources(uint32_t width, uint32_t height)
{
	auto makeFaceView = [this](::VkImage image, uint32_t layer, uint32_t mip) -> ::VkImageView
	{
		VkImageViewCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = image;
		info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		info.subresourceRange = Subresources::Single(VK_IMAGE_ASPECT_COLOR_BIT, mip, layer);
		::VkImageView view = VK_NULL_HANDLE;
		if (m_Device.GetFunctions().CreateImageView(m_Device.Handle(), &info, nullptr, &view) != VK_SUCCESS)
			FG_CORE_ERROR("VkRenderer: cubemap face view creation failed");
		return view;
	};

	m_EnvWidth = width;
	m_EnvHeight = height;
	m_PrefilterMipCount = 1;
	for (uint32_t s = m_PrefilterSize; s > 1; s >>= 1)
		++m_PrefilterMipCount;

	m_EnvImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
		VkExtent3D{ width, height, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, "IBL.EnvMap", m_EnvAlloc);
	m_IrradianceImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
		VkExtent3D{ m_IrradianceSize, m_IrradianceSize, 1 }, 1, 6, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, "IBL.Irradiance", m_IrradianceAlloc);
	m_PrefilterImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
		VkExtent3D{ m_PrefilterSize, m_PrefilterSize, 1 }, m_PrefilterMipCount, 6, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, "IBL.Prefilter", m_PrefilterAlloc);
	if (m_EnvImage == VK_NULL_HANDLE || m_IrradianceImage == VK_NULL_HANDLE || m_PrefilterImage == VK_NULL_HANDLE)
		return false;

	m_EnvImageView = CreateViewImpl(m_Device, m_EnvImage, VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);
	m_IrradianceView = CreateViewImpl(m_Device, m_IrradianceImage, VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_ASPECT_COLOR_BIT, 6, VK_IMAGE_VIEW_TYPE_CUBE);
	{
		VkImageViewCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		info.image = m_PrefilterImage;
		info.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		info.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		info.subresourceRange = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, m_PrefilterMipCount, 6);
		m_Device.GetFunctions().CreateImageView(m_Device.Handle(), &info, nullptr, &m_PrefilterView);
	}
	if (m_EnvImageView == VK_NULL_HANDLE || m_IrradianceView == VK_NULL_HANDLE || m_PrefilterView == VK_NULL_HANDLE)
		return false;

	m_IrradianceFaceViews.resize(6);
	m_IrradianceFaceFramebuffers.resize(6);
	for (uint32_t face = 0; face < 6; ++face)
	{
		m_IrradianceFaceViews[face] = makeFaceView(m_IrradianceImage, face, 0);
		if (m_IrradianceFaceViews[face] == VK_NULL_HANDLE)
			return false;
		m_IrradianceFaceFramebuffers[face] = CreateFramebufferImpl(m_Device, m_RenderPassCubeFace,
			std::span<const ::VkImageView>(&m_IrradianceFaceViews[face], 1), m_IrradianceSize, m_IrradianceSize);
		if (m_IrradianceFaceFramebuffers[face] == VK_NULL_HANDLE)
			return false;
	}

	m_PrefilterFaceViews.resize(m_PrefilterMipCount * 6);
	m_PrefilterFaceFramebuffers.resize(m_PrefilterMipCount * 6);
	for (uint32_t mip = 0; mip < m_PrefilterMipCount; ++mip)
	{
		const uint32_t mipSize = std::max(1u, m_PrefilterSize >> mip);
		for (uint32_t face = 0; face < 6; ++face)
		{
			::VkImageView& view = m_PrefilterFaceViews[mip * 6 + face];
			view = makeFaceView(m_PrefilterImage, face, mip);
			if (view == VK_NULL_HANDLE)
				return false;
			m_PrefilterFaceFramebuffers[mip * 6 + face] = CreateFramebufferImpl(m_Device, m_RenderPassCubeFace,
				std::span<const ::VkImageView>(&view, 1), mipSize, mipSize);
			if (m_PrefilterFaceFramebuffers[mip * 6 + face] == VK_NULL_HANDLE)
				return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Uploads (Phase 2 staging)
// ---------------------------------------------------------------------------

bool VkRenderer::UploadMesh(const Ref<Mesh>& mesh)
{
	const VkMesh* cpu = dynamic_cast<const VkMesh*>(mesh.get());
	if (!cpu)
	{
		FG_CORE_ERROR("VkRenderer: mesh is not a VkMesh; the renderer cannot upload it");
		return false;
	}
	const std::vector<Vertex>& vertices = cpu->GetVertices();
	const std::vector<uint32_t>& indices = cpu->GetIndices();
	if (vertices.empty() || indices.empty())
	{
		FG_CORE_ERROR("VkRenderer: mesh has no geometry to upload");
		return false;
	}

	const size_t vertexBytes = vertices.size() * sizeof(Vertex);
	const size_t indexBytes = indices.size() * sizeof(uint32_t);

	Ref<Buffer> vertexStaging = m_Staging->AddStagingBuffer(vertices.data(), vertexBytes);
	Ref<Buffer> indexStaging = m_Staging->AddStagingBuffer(indices.data(), indexBytes);
	if (!vertexStaging || !indexStaging)
		return false;

	VkBufferCreateInfo vertexInfo{};
	vertexInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexInfo.size = vertexBytes;
	vertexInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	vertexInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VkBufferCreateInfo indexInfo{};
	indexInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexInfo.size = indexBytes;
	indexInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	indexInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	BufferAllocationInfo vertexAlloc{};
	BufferAllocationInfo indexAlloc{};
	AllocationCreateInfo alloc{};
	alloc.Preference = MemoryPreference::PreferGpu;
	alloc.RequiredProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	if (!m_Allocator->AllocateBuffer("Mesh.VBO", vertexInfo, alloc, vertexAlloc) ||
		!m_Allocator->AllocateBuffer("Mesh.IBO", indexInfo, alloc, indexAlloc))
		return false;

	::VkCommandBuffer cmd = m_Staging->NextCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return false;
	VkCommandBufferBeginInfo begin{};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	m_Device.GetFunctions().BeginCommandBuffer(cmd, &begin);

	BufferBarrierBuilder bufferBarrier{};
	bufferBarrier.SrcAccess = VK_ACCESS_HOST_WRITE_BIT;
	bufferBarrier.DstAccess = VK_ACCESS_TRANSFER_READ_BIT;
	bufferBarrier.SrcStages = VK_PIPELINE_STAGE_HOST_BIT;
	bufferBarrier.DstStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
	RecordBufferBarrier(m_Device, cmd, bufferBarrier, vertexStaging->Handle());
	RecordBufferBarrier(m_Device, cmd, bufferBarrier, indexStaging->Handle());

	const VkBufferCopy vertexCopy{ 0, 0, vertexBytes };
	m_Device.GetFunctions().CmdCopyBuffer(cmd, vertexStaging->Handle(), vertexAlloc.Buffer, 1, &vertexCopy);
	const VkBufferCopy indexCopy{ 0, 0, indexBytes };
	m_Device.GetFunctions().CmdCopyBuffer(cmd, indexStaging->Handle(), indexAlloc.Buffer, 1, &indexCopy);

	m_Device.GetFunctions().EndCommandBuffer(cmd);

	// Synchronous: the first frame's draw reads this geometry, so the upload
	// must finish before the frame's command buffer submits.
	if (!m_Staging->Submit() || !m_Staging->Wait())
		return false;

	MeshGpu gpu{};
	gpu.VertexBuffer = vertexAlloc.Buffer;
	gpu.IndexBuffer = indexAlloc.Buffer;
	gpu.VertexAlloc = static_cast<VmaAllocation>(vertexAlloc.Allocation.Handle);
	gpu.IndexAlloc = static_cast<VmaAllocation>(indexAlloc.Allocation.Handle);
	gpu.IndexCount = static_cast<uint32_t>(indices.size());
	m_MeshCache.emplace(mesh.get(), gpu);

#if defined(FG_DEBUG)
	SetDebugName(m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(gpu.VertexBuffer), "Mesh.VBO");
	SetDebugName(m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(gpu.IndexBuffer), "Mesh.IBO");
#endif
	return true;
}

bool VkRenderer::UploadTexture(const Ref<Texture>& texture)
{
	const VkTexture* cpu = dynamic_cast<const VkTexture*>(texture.get());
	if (!cpu)
	{
		FG_CORE_ERROR("VkRenderer: texture is not a VkTexture; the renderer cannot upload it");
		return false;
	}

	const uint32_t width = cpu->GetWidth();
	const uint32_t height = cpu->GetHeight();
	if (width == 0 || height == 0)
		return false;

	// The upload payload: RGBA8 for LDR, RGBA16F for CPU float radiance.
	std::vector<uint8_t> payload;
	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	uint32_t blockBytes = 4;
	if (cpu->IsFloat())
	{
		const std::vector<float>& floats = cpu->GetFloatRgb();
		const size_t pixels = static_cast<size_t>(width) * height;
		if (floats.size() < pixels * 3)
			return false;
		std::vector<uint16_t> halves(pixels * 4);
		for (size_t i = 0; i < pixels; ++i)
		{
			halves[i * 4 + 0] = FloatToHalf(floats[i * 3 + 0]);
			halves[i * 4 + 1] = FloatToHalf(floats[i * 3 + 1]);
			halves[i * 4 + 2] = FloatToHalf(floats[i * 3 + 2]);
			halves[i * 4 + 3] = FloatToHalf(1.0f);
		}
		payload.resize(pixels * 8);
		std::memcpy(payload.data(), halves.data(), payload.size());
		format = VK_FORMAT_R16G16B16A16_SFLOAT;
		blockBytes = 8;
	}
	else
	{
		payload = cpu->GetRgba8();
		if (payload.size() != static_cast<size_t>(width) * height * 4)
			return false;
	}

	// The copy plan (pure): row padding removed, regions batched.
	const UploadImageLayout level{ width, height, 1 };
	UploadTextureDesc desc{};
	desc.TexelBlockBytes = blockBytes;
	desc.ArrayLayers = 1;
	desc.FaceCount = 1;
	desc.Levels = std::span<const UploadImageLayout>(&level, 1);
	const UploadPlan plan = BuildUploadPlan(desc);
	if (plan.RequiredBufferSizeBytes == 0)
		return false;

	Ref<Buffer> staging = m_Staging->AddStagingBuffer(plan.RequiredBufferSizeBytes);
	if (!staging)
		return false;
	{
		MappedMemory mapped = staging->Map();
		if (!mapped.IsValid())
			return false;
		std::span<std::byte> destination = mapped.AsSpan();
		for (const StagingRegion& region : plan.StageRegions)
		{
			if (region.SourceOffset + region.SizeInBytes > payload.size() ||
				region.DestinationOffset + region.SizeInBytes > destination.size())
				return false;
			std::memcpy(destination.data() + region.DestinationOffset,
				payload.data() + region.SourceOffset, region.SizeInBytes);
		}
		mapped.Flush();
	}

	ImageAllocationInfo imageAlloc{};
	{
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = format;
		imageInfo.extent = VkExtent3D{ width, height, 1 };
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		AllocationCreateInfo alloc{};
		alloc.Preference = MemoryPreference::PreferGpu;
		alloc.RequiredProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		if (!m_Allocator->AllocateImage("Texture", imageInfo, alloc, imageAlloc))
			return false;
	}

	::VkCommandBuffer cmd = m_Staging->NextCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return false;
	VkCommandBufferBeginInfo begin{};
	begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	m_Device.GetFunctions().BeginCommandBuffer(cmd, &begin);

	BufferBarrierBuilder bufferBarrier{};
	bufferBarrier.SrcAccess = VK_ACCESS_HOST_WRITE_BIT;
	bufferBarrier.DstAccess = VK_ACCESS_TRANSFER_READ_BIT;
	bufferBarrier.SrcStages = VK_PIPELINE_STAGE_HOST_BIT;
	bufferBarrier.DstStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
	RecordBufferBarrier(m_Device, cmd, bufferBarrier, staging->Handle());

	ImageBarrierBuilder imageBarrier{};
	imageBarrier.SubresourceRange = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
	imageBarrier.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageBarrier.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	imageBarrier.DstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
	RecordUploadPreBarriers(m_Device, cmd, staging->Handle(), imageAlloc.Image, imageBarrier);

	std::vector<VkBufferImageCopy> copies;
	copies.reserve(plan.CopyRegions.size());
	for (const UploadRegion& region : plan.CopyRegions)
	{
		VkBufferImageCopy copy{};
		copy.bufferOffset = region.BufferOffset;
		copy.imageSubresource = Subresources::FullLayers(VK_IMAGE_ASPECT_COLOR_BIT, region.BaseLevel,
			region.BaseLayer, region.LayerCount);
		copy.imageExtent = VkExtent3D{ width, height, 1 };
		copies.push_back(copy);
	}
	m_Device.GetFunctions().CmdCopyBufferToImage(cmd, staging->Handle(), imageAlloc.Image,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(copies.size()), copies.data());

	imageBarrier.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	imageBarrier.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	imageBarrier.SrcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
	imageBarrier.DstAccess = VK_ACCESS_SHADER_READ_BIT;
	imageBarrier.SrcStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
	imageBarrier.DstStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	RecordUploadPostBarrier(m_Device, cmd, imageAlloc.Image, imageBarrier);

	m_Device.GetFunctions().EndCommandBuffer(cmd);
	if (!m_Staging->Submit() || !m_Staging->Wait())
		return false;

	::VkImageView view = VK_NULL_HANDLE;
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = imageAlloc.Image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
		if (m_Device.GetFunctions().CreateImageView(m_Device.Handle(), &viewInfo, nullptr, &view) != VK_SUCCESS)
			return false;
	}

	TextureGpu gpu{};
	gpu.Image = imageAlloc.Image;
	gpu.View = view;
	gpu.Alloc = static_cast<VmaAllocation>(imageAlloc.Allocation.Handle);
	gpu.Width = width;
	gpu.Height = height;
	m_TextureCache.emplace(texture.get(), gpu);
	return true;
}

VkRenderer::MeshGpu* VkRenderer::GetMeshGpu(const Ref<Mesh>& mesh)
{
	if (!mesh)
		return nullptr;
	auto cached = m_MeshCache.find(mesh.get());
	if (cached != m_MeshCache.end())
		return &cached->second;
	if (!UploadMesh(mesh))
		return nullptr;
	cached = m_MeshCache.find(mesh.get());
	return cached != m_MeshCache.end() ? &cached->second : nullptr;
}

const VkRenderer::TextureGpu* VkRenderer::GetTextureGpu(const Ref<Texture>& texture)
{
	if (!texture)
		return nullptr;
	auto cached = m_TextureCache.find(texture.get());
	if (cached != m_TextureCache.end())
		return &cached->second;
	if (!UploadTexture(texture))
		return nullptr;
	cached = m_TextureCache.find(texture.get());
	return cached != m_TextureCache.end() ? &cached->second : nullptr;
}

void VkRenderer::DestroyUploadCaches()
{
	if (m_Allocator)
	{
		for (auto& [key, gpu] : m_MeshCache)
		{
			(void)key;
			for (auto& [buffer, allocation] : std::array<std::pair<::VkBuffer, VmaAllocation>, 2>{
					std::pair{ gpu.VertexBuffer, gpu.VertexAlloc },
					std::pair{ gpu.IndexBuffer, gpu.IndexAlloc } })
			{
				if (buffer == VK_NULL_HANDLE)
					continue;
				BufferAllocationInfo info{};
				info.Buffer = buffer;
				info.Allocation.Handle = allocation;
				info.Allocation.Allocator = m_Allocator.get();
				(void)m_Allocator->FreeAllocation(info);
			}
		}
		for (auto& [key, gpu] : m_TextureCache)
		{
			(void)key;
			if (gpu.View != VK_NULL_HANDLE)
				m_Device.GetFunctions().DestroyImageView(m_Device.Handle(), gpu.View, nullptr);
			if (gpu.Image == VK_NULL_HANDLE)
				continue;
			ImageAllocationInfo info{};
			info.Image = gpu.Image;
			info.Allocation.Handle = gpu.Alloc;
			info.Allocation.Allocator = m_Allocator.get();
			(void)m_Allocator->FreeAllocation(info);
		}
	}
	m_MeshCache.clear();
	m_TextureCache.clear();
}

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

bool VkRenderer::CaptureSwapchainImage(uint32_t imageIndex, const std::string& path)
{
	if (!m_DeviceReady || imageIndex >= m_Swapchain.GetImages().size())
	{
		FG_CORE_ERROR("VkRenderer: no swapchain image {0} to capture", imageIndex);
		return false;
	}

	const ::VkImage image = m_Swapchain.GetImages()[imageIndex];
	const uint32_t width = m_Swapchain.GetExtent().width;
	const uint32_t height = m_Swapchain.GetExtent().height;
	const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width) * height * 4;

	// Host-coherent readback buffer through the engine's allocator.
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = bufferSize;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	BufferAllocationInfo readback{};
	AllocationCreateInfo alloc{};
	alloc.Preference = MemoryPreference::PreferCpu;
	alloc.RequiredProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	if (!m_Allocator->AllocateBuffer("Capture.Readback", bufferInfo, alloc, readback))
		return false;

	// All rendering must be finished before the image can be copied.
	WaitForGpuIdle();

	// One-shot command buffer in its own pool: capture is rare, and it must
	// not touch the frame loop's pool mid-frame.
	::VkCommandPool pool = VK_NULL_HANDLE;
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = m_GraphicsFamily;
	if (m_Device.GetFunctions().CreateCommandPool(m_Device.Handle(), &poolInfo, nullptr, &pool) != VK_SUCCESS)
		return false;

	::VkCommandBuffer cmd = VK_NULL_HANDLE;
	VkCommandBufferAllocateInfo commandInfo{};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandInfo.commandPool = pool;
	commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandInfo.commandBufferCount = 1;
	const bool allocated = m_Device.GetFunctions().AllocateCommandBuffers(m_Device.Handle(), &commandInfo, &cmd) == VK_SUCCESS;

	bool ok = false;
	if (allocated)
	{
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		m_Device.GetFunctions().BeginCommandBuffer(cmd, &begin);

		// The final pass rendered into this image and presented it; it is in
		// PRESENT_SRC. Transitioning from UNDEFINED instead would *discard*
		// the rendered contents - the copy would read an image the driver was
		// free to treat as garbage.
		ImageBarrierBuilder toSource{};
		toSource.SubresourceRange = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
		toSource.OldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		toSource.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		toSource.SrcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		toSource.DstAccess = VK_ACCESS_TRANSFER_READ_BIT;
		toSource.SrcStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		toSource.DstStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
		RecordImageBarrier(m_Device, cmd, toSource, image);

		VkBufferImageCopy region{};
		region.imageSubresource = Subresources::FullLayers(VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1);
		region.imageExtent = VkExtent3D{ width, height, 1 };
		m_Device.GetFunctions().CmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			readback.Buffer, 1, &region);

		ImageBarrierBuilder back{};
		back.SubresourceRange = toSource.SubresourceRange;
		back.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		back.NewLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		back.SrcAccess = VK_ACCESS_TRANSFER_READ_BIT;
		back.DstAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		back.SrcStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
		back.DstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		RecordImageBarrier(m_Device, cmd, back, image);

		m_Device.GetFunctions().EndCommandBuffer(cmd);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &cmd;
		if (m_GraphicsQueue->Submit(std::span<const VkSubmitInfo>(&submitInfo, 1)))
		{
			(void)m_GraphicsQueue->WaitIdle();

			MappedMemory mapped = MappedMemory::Map(AllocationMapInfo{ readback.Allocation, MapRegion{ 0, WholeSize } });
			if (mapped.IsValid())
			{
				mapped.Invalidate();
				const uint8_t* src = static_cast<const uint8_t*>(mapped.Data());
				std::ofstream out(path, std::ios::binary);
				if (out.is_open())
				{
					out << "P6\n" << width << " " << height << "\n255\n";
					for (uint32_t y = 0; y < height; ++y)
					{
						const uint8_t* row = src + static_cast<size_t>(y) * width * 4;
						for (uint32_t x = 0; x < width; ++x)
						{
							const uint8_t* pixel = row + x * 4;
							const char rgb[3] = {
								static_cast<char>(pixel[2]),
								static_cast<char>(pixel[1]),
								static_cast<char>(pixel[0])
							};
							out.write(rgb, 3);
						}
					}
					ok = out.good();
				}
			}
		}
	}

	{
		BufferAllocationInfo info{};
		info.Buffer = readback.Buffer;
		info.Allocation = readback.Allocation;
		(void)m_Allocator->FreeAllocation(info);
	}
	if (pool != VK_NULL_HANDLE)
		m_Device.GetFunctions().DestroyCommandPool(m_Device.Handle(), pool, nullptr);

	if (!ok)
		FG_CORE_ERROR("VkRenderer: swapchain capture to '{0}' failed", path);
	return ok;
}

}}
