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

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
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


	[[nodiscard]] float HalfToFloat(uint16_t h)
	{
		const uint32_t sign = (h & 0x8000U) << 16U;
		const uint32_t exponent = (h >> 10U) & 0x1FU;
		const uint32_t mantissa = h & 0x3FFU;
		uint32_t bits = 0;
		if (exponent == 0)
		{
			if (mantissa == 0)
				bits = sign;
			else
			{
				// subnormal: normalize
				uint32_t m = mantissa;
				int32_t e = -126;
				while ((m & 0x400U) == 0) { m <<= 1; --e; }
				m &= 0x3FFU;
				bits = sign | (static_cast<uint32_t>(e + 127) << 23) | (m << 13);
			}
		}
		else if (exponent == 0x1F)
			bits = sign | 0x7F800000U | (mantissa << 13);
		else
			bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
		float f;
		std::memcpy(&f, &bits, sizeof(f));
		return f;
	}

		// IEEE binary16: the HDR upload converts CPU float radiance to RGBA16F.
	// Round-to-nearest-even; saturates to infinity past half's exponent range.
	[[nodiscard]] float HalfToFloat(uint16_t h);

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

	// GL -> Vulkan clip space: the engine's matrices come from
	// glm::perspective (OpenGL NDC, +Y up, depth [-1,1]) and the shaders
	// write gl_Position through them unchanged. The nvrhi renderer it
	// replaces flipped Y in the viewport automatically; raw Vulkan does
	// not, so the uploaded projections carry the flip themselves. It is a
	// clip-space flip - the projection's y row negated - so it multiplies
	// the matrix from the left: applied on the right it would mirror the
	// world around y = 0 instead, moving the geometry where no camera
	// looks at it. The shadow side (which samples with the same matrix)
	// and SSAO (which inverts it) stay consistent with the rasterizer.
	[[nodiscard]] const glm::mat4& FlipY()
	{
		static const glm::mat4 flip = glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, -1.0f, 1.0f));
		return flip;
	}

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
	// The 4-vertex fullscreen quad, drawn indexed as two triangles from the
	// renderer's quad buffers (the pipeline bakes the quad vertex layout, so
	// the buffers must be bound for aPosition/aUV to exist).
	void DrawFullscreenQuad(const Device& device, ::VkCommandBuffer cmd, ::VkPipeline pipeline,
		::VkPipelineLayout layout, ::VkDescriptorSet set,
		::VkBuffer vertexBuffer, ::VkBuffer indexBuffer, uint32_t width, uint32_t height)
	{
		device.GetFunctions().CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
		device.GetFunctions().CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
		const ::VkDeviceSize offset = 0;
		device.GetFunctions().CmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
		device.GetFunctions().CmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		SetViewportScissor(device, cmd, width, height);
		device.GetFunctions().CmdDrawIndexed(cmd, 6, 1, 0, 0, 0);
	}

	[[nodiscard]] VkDescriptorBufferInfo BufferRange(::VkBuffer buffer, size_t offset, size_t size)
	{
		return VkDescriptorBufferInfo{ buffer, static_cast<VkDeviceSize>(offset), static_cast<VkDeviceSize>(size) };
	}

	// A descriptor names the layout its image is in, and the validation
	// layer rejects the draw when the two disagree: a colour target is
	// SHADER_READ_ONLY_OPTIMAL, a depth target is
	// DEPTH_STENCIL_READ_ONLY_OPTIMAL. The default is the colour case;
	// a depth image says so.
	[[nodiscard]] VkDescriptorImageInfo ImageBinding(::VkSampler sampler, ::VkImageView view,
		VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		return VkDescriptorImageInfo{ sampler, view, layout };
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
	data.ViewProjection = FlipY() * camera.ViewProjection;
	data.ShadowViewProjection = FlipY() * camera.ShadowViewProjection;
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
	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;
	const std::array<VkClearValue, 2> clears{
		VkClearValue{ .color = { kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3] } },
		VkClearValue{ .depthStencil = { 1.0f, 0 } },
	};
	BeginRenderPass(m_Device, cmd, m_RenderPassHdr, m_FbHdr,
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
	// ObjectData at this draw's copy. The ring wraps inside this image's
	// region only, so a submitted frame's data is never overwritten.
	const size_t slotOffset = static_cast<size_t>(m_ObjectSlot) * ObjectSlotBytes;
	const uint32_t regionBase = m_CurrentImageIndex * ObjectSlotsPerFrame;
	m_ObjectSlot = regionBase + (m_ObjectSlot + 1U - regionBase) % ObjectSlotsPerFrame;
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
	{
		const glm::mat4 flipped = FlipY() * shadowViewProjection;
		std::memcpy(m_ShadowCameraUboData, &flipped, sizeof(glm::mat4));
	}

	m_CurrentTargetWidth = m_ShadowMapSize;
	m_CurrentTargetHeight = m_ShadowMapSize;

	EndActiveRenderPass();
	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;
	const std::array<VkClearValue, 1> clears{
		VkClearValue{ .depthStencil = { 1.0f, 0 } },
	};
	BeginRenderPass(m_Device, cmd, m_RenderPassShadow, m_FbShadow,
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

	const size_t slotOffset = static_cast<size_t>(m_ObjectSlot) * ObjectSlotBytes;
	const uint32_t regionBase = m_CurrentImageIndex * ObjectSlotsPerFrame;
	m_ObjectSlot = regionBase + (m_ObjectSlot + 1U - regionBase) % ObjectSlotsPerFrame;
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
	params.InvViewProjection = glm::inverse(FlipY() * viewProjection);
	params.Intensity = 1.0f;
	params.Radius = 0.5f;
	params.Bias = 0.002f;
	params.Power = 1.5f;
	params.TexelSize = glm::vec2(1.0f / static_cast<float>(m_RenderWidth), 1.0f / static_cast<float>(m_RenderHeight));
	std::memcpy(m_SsaoUboData, &params, sizeof(params));

	const std::array<::VkImageView, 1> views{ m_DepthView };
	const std::array<VkImageLayout, 1> layouts{ VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
	const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_SsaoUbo, 0, sizeof(SsaoParams)) };
	::VkDescriptorSet set = BuildPostSet(m_SetLayoutSsao, views, layouts, samplers, buffers);
	if (set == VK_NULL_HANDLE)
		return;

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;
	const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
	BeginRenderPass(m_Device, cmd, m_RenderPassR8, m_FbSsao, m_RenderWidth, m_RenderHeight, clears);
	DrawFullscreenQuad(m_Device, cmd, m_PipelineSsao, m_LayoutSsao, set, m_QuadVertexBuffer, m_QuadIndexBuffer, m_RenderWidth, m_RenderHeight);
	m_Device.GetFunctions().CmdEndRenderPass(cmd);
}

void VkRenderer::ClearHdrTarget()
{
	EndActiveRenderPass();
	if (!HasTargets())
		return;
	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;
	const std::array<VkClearValue, 2> clears{
		VkClearValue{ .color = { kClearColor[0], kClearColor[1], kClearColor[2], kClearColor[3] } },
		VkClearValue{ .depthStencil = { 1.0f, 0 } },
	};
	BeginRenderPass(m_Device, cmd, m_RenderPassHdr, m_FbHdr,
		m_RenderWidth, m_RenderHeight, clears);
	m_Device.GetFunctions().CmdEndRenderPass(cmd);
}

void VkRenderer::SubmitFrameOnly()
{
	EndActiveRenderPass();
	if (!m_DeviceReady || !m_FrameLoop || m_CurrentImageIndex == UINT32_MAX)
		return;
	(void)m_Staging->Submit();
	(void)m_Staging->Wait();
	const PresentOutcome outcome = m_FrameLoop->EndFrame(m_CurrentImageIndex);
	FG_CORE_INFO("VkRenderer diag: SubmitFrameOnly image {0} outcome {1}", m_CurrentImageIndex, static_cast<int>(outcome));
}

void VkRenderer::DiagDrawSsaoIntoTonemap()
{
	EndActiveRenderPass();
	if (!HasTargets() || m_PipelineSsao == VK_NULL_HANDLE)
		return;

	VkRenderingInfo rendering{};
	rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	rendering.renderArea = VkRect2D{ {0, 0}, {m_PresentWidth, m_PresentHeight} };
	rendering.layerCount = 1;
	rendering.colorAttachmentCount = 1;
	VkRenderingAttachmentInfo attachment{};
	attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	attachment.imageView = m_TonemapView;
	attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.clearValue = VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } };
	rendering.pColorAttachments = &attachment;

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;
	vkCmdBeginRendering(cmd, &rendering);

	const std::array<::VkImageView, 1> views{ m_DepthView };
	const std::array<VkImageLayout, 1> layouts{ VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
	const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
	const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_SsaoUbo, 0, sizeof(SsaoParams)) };
	::VkDescriptorSet set = BuildPostSet(m_SetLayoutSsao, views, layouts, samplers, buffers);
	if (set != VK_NULL_HANDLE)
		DrawFullscreenQuad(m_Device, cmd, m_PipelineSsao, m_LayoutSsao, set,
			m_QuadVertexBuffer, m_QuadIndexBuffer, m_PresentWidth, m_PresentHeight);

	vkCmdEndRendering(cmd);
	// Leave the tonemap target in the layout the capture expects.
	ImageBarrierBuilder toShader{};
	toShader.SubresourceRange = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
	toShader.OldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	toShader.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toShader.SrcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	toShader.DstAccess = VK_ACCESS_SHADER_READ_BIT;
	toShader.SrcStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	toShader.DstStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	RecordImageBarrier(m_Device, cmd, toShader, m_TonemapImage);
}

void VkRenderer::ClearHdrTargetRed()
{
	EndActiveRenderPass();
	if (!HasTargets())
		return;
	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;
	const std::array<VkClearValue, 2> clears{
		VkClearValue{ .color = { 1.0f, 0.0f, 0.0f, 1.0f } },
		VkClearValue{ .depthStencil = { 1.0f, 0 } },
	};
	BeginRenderPass(m_Device, cmd, m_RenderPassHdr, m_FbHdr,
		m_RenderWidth, m_RenderHeight, clears);
	m_Device.GetFunctions().CmdEndRenderPass(cmd);
	(void)m_Staging->Submit();
	(void)m_Staging->Wait();
	(void)m_GraphicsQueue->WaitIdle();
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
	bloomParams.Threshold = std::getenv("FG_DIAG_BLOOM_FORCE") ? 0.0f : defaults.Threshold;
	bloomParams.Knee = defaults.Knee;
	bloomParams.Scale = defaults.Scale;
	std::memcpy(m_BloomUboData, &bloomParams, sizeof(bloomParams));

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;

	// Step 1: soft-threshold luminance + 13-tap downsample, full-res HDR -> half-res.
	{
		const std::array<::VkImageView, 1> views{ m_HdrView };
		const std::array<VkImageLayout, 1> layouts{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
		const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_BloomUbo, 0, sizeof(GpuBloomParams)) };
		::VkDescriptorSet set = BuildPostSet(m_SetLayoutBloom, views, layouts, samplers, buffers);
		if (set == VK_NULL_HANDLE)
			return;
		const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
		BeginRenderPass(m_Device, cmd, m_RenderPassColor16, m_FbBloomHalf, halfWidth, halfHeight, clears);
		DrawFullscreenQuad(m_Device, cmd, m_PipelineBloom, m_LayoutBloom, set, m_QuadVertexBuffer, m_QuadIndexBuffer, halfWidth, halfHeight);
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
		const std::array<VkImageLayout, 1> layouts{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
		const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_BlurUbo, 0, sizeof(GpuBlurParams)) };
		::VkDescriptorSet set = BuildPostSet(m_SetLayoutBloom, views, layouts, samplers, buffers);
		if (set == VK_NULL_HANDLE)
			return;

		const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
		BeginRenderPass(m_Device, cmd, m_RenderPassColor16, targets[iteration], halfWidth, halfHeight, clears);
		DrawFullscreenQuad(m_Device, cmd, m_PipelineBlur, m_LayoutBloom, set, m_QuadVertexBuffer, m_QuadIndexBuffer, halfWidth, halfHeight);
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

	const bool diagWhite = std::getenv("FG_DIAG_TONEMAP_WHITE") != nullptr;
	const std::array<::VkImageView, 2> views{ diagWhite ? m_WhiteView : m_HdrView, m_BloomAView };
	const std::array<VkImageLayout, 2> layouts{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
	const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_HdrUbo, 0, sizeof(GpuHdrParams)) };
	::VkDescriptorSet set = BuildPostSet(m_SetLayoutTonemap, views, layouts, samplers, buffers);
	if (set == VK_NULL_HANDLE)
		return;

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;

	const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
	BeginRenderPass(m_Device, cmd, m_RenderPassColor16, m_FbTonemap, m_PresentWidth, m_PresentHeight, clears);
	DrawFullscreenQuad(m_Device, cmd, m_PipelineTonemap, m_LayoutTonemap, set, m_QuadVertexBuffer, m_QuadIndexBuffer, m_PresentWidth, m_PresentHeight);
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
	const std::array<VkImageLayout, 2> layouts{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
	const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_FinalUbo, 0, sizeof(GpuFinalParams)) };
	::VkDescriptorSet set = BuildPostSet(m_SetLayoutFinal, views, layouts, samplers, buffers);
	if (set == VK_NULL_HANDLE)
		return;

	::VkCommandBuffer cmd = CurrentCommandBuffer();
	if (cmd == VK_NULL_HANDLE)
		return;

	// The present render pass has DONT_CARE load: the whole quad covers it.
	// It is left OPEN: the ImGui backend records its draw commands onto this
	// same command buffer after BeginFinalPass, and it never begins rendering
	// itself - the app's active render pass is its render target. Closing
	// the pass here records those draws outside any render pass, which is
	// undefined: the driver drops them and the overlay never appears
	// (measured: Tetris HUD invisible, draw data valid). EndScene closes
	// the pass, EndFrame takes the image to PRESENT_SRC.
	BeginRenderPass(m_Device, cmd, m_RenderPassPresent, m_SwapchainFramebuffers[m_CurrentImageIndex],
		m_PresentWidth, m_PresentHeight, {});
	DrawFullscreenQuad(m_Device, cmd, m_PipelineFinal, m_LayoutFinal, set, m_QuadVertexBuffer, m_QuadIndexBuffer, m_PresentWidth, m_PresentHeight);
	m_RenderPassOpen = true;
}

// ---------------------------------------------------------------------------
// Descriptor set builders
// ---------------------------------------------------------------------------

namespace {

	// Every set built here is recorded into the current frame's command
	// buffer, so a set requested with no frame begun has no consumer. The
	// per-frame pool reset only runs in BeginFrame: allocating these sets
	// on frames that never begin drains the pool's accounting with nothing
	// to show for it (measured: 512 refused allocations after a window
	// close while the app spun on failed acquires).
	[[nodiscard]] bool FrameRecording(::VkCommandBuffer cmd)
	{
		return cmd != VK_NULL_HANDLE;
	}

}  // namespace

::VkDescriptorSet VkRenderer::BuildPbrSet(::VkBuffer objectBuffer, size_t objectOffset,
	::VkImageView albedoView, bool probeDraw)
{
	if (!FrameRecording(CurrentCommandBuffer()))
		return VK_NULL_HANDLE;

	std::vector<::VkDescriptorSet> sets;
	std::string reason;
	if (!CurrentDescriptorPool().AllocateSets(m_SetLayoutPbr, 1, sets, reason))
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
		// The shadow map is a depth image: it is left in
		// DEPTH_STENCIL_READ_ONLY_OPTIMAL by the shadow pass, and the
		// descriptor has to say so.
		ImageBinding(m_SamplerShadow, m_ShadowView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
		ImageBinding(m_SamplerLinear, VK_NULL_HANDLE),
		ImageBinding(m_SamplerLinear, envView),
		ImageBinding(m_SamplerLinear, irradianceView),
		ImageBinding(m_SamplerLinear, VK_NULL_HANDLE),
	};
	(void)images[2];   // binding 6: the linear sampler alone (no view)
	(void)images[5];

	const std::array<VkWriteDescriptorSet, 12> writes{
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers[0], nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers[1], nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[0], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 3, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &images[0], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[1], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 5, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &images[1], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 6, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &images[2], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 7, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers[2], nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 8, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[3], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 9, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &images[3], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 10, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &images[4], nullptr, nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 11, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &images[4], nullptr, nullptr },
	};
	m_Device.GetFunctions().UpdateDescriptorSets(m_Device.Handle(),
		static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	return set;
}

::VkDescriptorSet VkRenderer::BuildShadowSet(::VkBuffer objectBuffer, size_t objectOffset)
{
	if (!FrameRecording(CurrentCommandBuffer()))
		return VK_NULL_HANDLE;

	std::vector<::VkDescriptorSet> sets;
	std::string reason;
	if (!CurrentDescriptorPool().AllocateSets(m_SetLayoutShadow, 1, sets, reason))
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
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers[0], nullptr },
		VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 4, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffers[1], nullptr },
	};
	m_Device.GetFunctions().UpdateDescriptorSets(m_Device.Handle(),
		static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	return set;
}

::VkDescriptorSet VkRenderer::BuildPostSet(const DescriptorSetLayout& layout,
	std::span<const ::VkImageView> views,
	std::span<const VkImageLayout> layouts,
	std::span<const ::VkSampler> samplers,
	std::span<const VkDescriptorBufferInfo> buffers)
{
	if (!FrameRecording(CurrentCommandBuffer()))
		return VK_NULL_HANDLE;

	std::vector<::VkDescriptorSet> sets;
	std::string reason;
	if (!CurrentDescriptorPool().AllocateSets(layout, 1, sets, reason))
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
			// The descriptor names the layout the image is in. A caller
			// that does not say which layout its views are in gets the
			// colour layout, which is what every colour target uses.
			const VkImageLayout imageLayout = viewIndex < layouts.size()
				? layouts[viewIndex] : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			++viewIndex;
			imageInfos.push_back(VkDescriptorImageInfo{ VK_NULL_HANDLE, view, imageLayout });
			writes.push_back(VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set,
				binding.binding, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &imageInfos.back(), nullptr, nullptr });
			break;
		}
		case VK_DESCRIPTOR_TYPE_SAMPLER:
		{
			const ::VkSampler sampler = samplerIndex < samplers.size() ? samplers[samplerIndex] : VK_NULL_HANDLE;
			++samplerIndex;
			imageInfos.push_back(VkDescriptorImageInfo{ sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED });
			writes.push_back(VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set,
				binding.binding, 0, 1, VK_DESCRIPTOR_TYPE_SAMPLER, &imageInfos.back(), nullptr, nullptr });
			break;
		}
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		{
			const VkDescriptorBufferInfo buffer = bufferIndex < buffers.size() ? buffers[bufferIndex]
				: VkDescriptorBufferInfo{ VK_NULL_HANDLE, 0, 0 };
			++bufferIndex;
			writes.push_back(VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set,
				binding.binding, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &buffer, nullptr });
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
		// The face framebuffers are indexed, so their count is checked before
		// they are: a probe that was never created has no framebuffers at all,
		// and reading one out of bounds hands the driver a garbage handle.
		if (cmd == VK_NULL_HANDLE || face >= m_ReflectionFaceFramebuffers.size())
			return;
		if (m_ReflectionFaceFramebuffers[face] == VK_NULL_HANDLE)
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

		// The post-upload transition is the helper's business: it owns the
		// layouts, the access masks and the stage masks, because they have
		// to be legal on the queue family the staging pool runs on.
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
			const std::array<VkImageLayout, 1> layouts{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
			const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_IrradianceUbo, 0, sizeof(GpuIrradianceParams)) };
			::VkDescriptorSet set = BuildPostSet(m_SetLayoutIrradiance, views, layouts, samplers, buffers);
			if (set == VK_NULL_HANDLE)
				return false;

			::VkCommandBuffer cmd = CurrentCommandBuffer();
			if (cmd == VK_NULL_HANDLE)
				return false;
			const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
			BeginRenderPass(m_Device, cmd, m_RenderPassCubeFace, m_IrradianceFaceFramebuffers[face],
				m_IrradianceSize, m_IrradianceSize, clears);
			DrawFullscreenQuad(m_Device, cmd, m_PipelineIrradiance, m_LayoutIrradiance, set,
				m_QuadVertexBuffer, m_QuadIndexBuffer, m_IrradianceSize, m_IrradianceSize);
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
				const std::array<VkImageLayout, 1> layouts{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
				const std::array<::VkSampler, 1> samplers{ m_SamplerLinear };
				const std::array<VkDescriptorBufferInfo, 1> buffers{ BufferRange(m_PrefilterUbo, 0, sizeof(GpuPrefilterParams)) };
				::VkDescriptorSet set = BuildPostSet(m_SetLayoutPrefilter, views, layouts, samplers, buffers);
				if (set == VK_NULL_HANDLE)
					return false;

				::VkCommandBuffer cmd = CurrentCommandBuffer();
				if (cmd == VK_NULL_HANDLE)
					return false;
				const std::array<VkClearValue, 1> clears{ VkClearValue{ .color = { 0.0f, 0.0f, 0.0f, 0.0f } } };
				BeginRenderPass(m_Device, cmd, m_RenderPassCubeFace,
					m_PrefilterFaceFramebuffers[mip * 6 + face], mipSize, mipSize, clears);
				DrawFullscreenQuad(m_Device, cmd, m_PipelinePrefilter, m_LayoutPrefilter, set, m_QuadVertexBuffer, m_QuadIndexBuffer, mipSize, mipSize);
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
	{
		FG_CORE_ERROR("VkRenderer: mesh upload: staging buffer failed ({0} vertices, {1} indices)", vertices.size(), indices.size());
		return false;
	}

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
	{
		FG_CORE_ERROR("VkRenderer: mesh upload: staging command buffer failed");
		return false;
	}
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

	// Synchronous upload: the staging pool's command buffers are
	// one-time-submit, so the copy must complete before the next
	// NextCommandBuffer() records into a fresh one - and the draw that
	// reads this geometry sits in the frame's own command buffer,
	// submitted after this copy, so the data is on the GPU before the
	// first read by construction.
	if (!m_Staging->Submit() || !m_Staging->Wait())
	{
		FG_CORE_ERROR("VkRenderer: mesh upload: staging submit/wait failed");
		return false;
	}

	MeshGpu gpu{};
	gpu.VertexBuffer = vertexAlloc.Buffer;
	gpu.IndexBuffer = indexAlloc.Buffer;
	gpu.VertexAlloc = static_cast<VmaAllocation>(vertexAlloc.Allocation.Handle);
	gpu.IndexAlloc = static_cast<VmaAllocation>(indexAlloc.Allocation.Handle);
	gpu.IndexCount = static_cast<uint32_t>(indices.size());
	m_MeshCache.emplace(mesh.get(), gpu);

	// Diagnostic: read the first uploaded vertex back and report it.
	if (std::getenv("FG_DIAG_VBO_READBACK"))
	{
		VkBufferCreateInfo rbInfo{};
		rbInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		rbInfo.size = sizeof(Vertex);
		rbInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		AllocationCreateInfo rbAlloc{};
		rbAlloc.Preference = MemoryPreference::PreferCpu;
		rbAlloc.RequiredProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
		BufferAllocationInfo rbOut{};
		if (m_Allocator->AllocateBuffer("Diag.VboReadback", rbInfo, rbAlloc, rbOut))
		{
			::VkCommandBuffer dcmd = m_Staging->NextCommandBuffer();
			if (dcmd != VK_NULL_HANDLE)
			{
				VkCommandBufferBeginInfo dbegin{};
				dbegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				dbegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				m_Device.GetFunctions().BeginCommandBuffer(dcmd, &dbegin);
				const VkBufferCopy copy{ 0, 0, sizeof(Vertex) };
				m_Device.GetFunctions().CmdCopyBuffer(dcmd, gpu.VertexBuffer, rbOut.Buffer, 1, &copy);
				m_Device.GetFunctions().EndCommandBuffer(dcmd);
				if (m_Staging->Submit() && m_Staging->Wait())
				{
					MappedMemory mapped = MappedMemory::Map(AllocationMapInfo{ rbOut.Allocation, MapRegion{ 0, WholeSize } });
					if (mapped.IsValid())
					{
						mapped.Invalidate();
						Vertex v{};
						std::memcpy(&v, mapped.Data(), sizeof(Vertex));
						FG_CORE_INFO("VkRenderer diag: VBO vertex0 pos ({0}, {1}, {2}) stride {3} nverts {4}",
							v.Position.x, v.Position.y, v.Position.z, m_VertexLayoutMesh.Stride, vertices.size());
					}
				}
			}
			BufferAllocationInfo freeInfo{};
			freeInfo.Buffer = rbOut.Buffer;
			freeInfo.Allocation = rbOut.Allocation;
			(void)m_Allocator->FreeAllocation(freeInfo);
		}
	}

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

	// The post-upload transition is the helper's business: it owns the
	// layouts, the access masks and the stage masks, because they have to
	// be legal on the queue family the staging pool runs on.
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
	{
		FG_CORE_ERROR("VkRenderer: mesh upload failed; the entity is not drawn");
		return nullptr;
	}
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


// ---------------------------------------------------------------------------
// Diagnostic target capture
// ---------------------------------------------------------------------------

bool VkRenderer::CaptureTargetImage(const ::VkImage image, uint32_t width, uint32_t height,
	bool isFloat, const std::string& path)
{
	if (!m_DeviceReady || image == VK_NULL_HANDLE || width == 0 || height == 0)
		return false;

	const uint32_t bytesPerTexel = isFloat ? 8 : 4;
	const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width) * height * bytesPerTexel;

	// The staging pool's command buffers are one-time-submit: a capture
	// recorded into them while the frame's mesh uploads are pending would
	// die at Submit with VK_ERROR_NOT_READY. A dedicated pool keeps the
	// capture independent.
	(void)0;

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

	WaitForGpuIdle();

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

		ImageBarrierBuilder toSource{};
		toSource.SubresourceRange = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
		toSource.OldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toSource.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		toSource.SrcAccess = VK_ACCESS_SHADER_READ_BIT;
		toSource.DstAccess = VK_ACCESS_TRANSFER_READ_BIT;
		toSource.SrcStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
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
		back.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		back.SrcAccess = VK_ACCESS_TRANSFER_READ_BIT;
		back.DstAccess = VK_ACCESS_SHADER_READ_BIT;
		back.SrcStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
		back.DstStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
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
						const uint8_t* row = src + static_cast<size_t>(y) * width * bytesPerTexel;
						for (uint32_t x = 0; x < width; ++x)
						{
							uint8_t rgb[3];
							if (isFloat)
							{
								const uint16_t* texel = reinterpret_cast<const uint16_t*>(row + x * 8);
								// PPM carries gamma-encoded bytes; the target is
								// linear, so a quick approximation is fine for
								// eyeballing "is anything there at all".
								for (int c = 0; c < 3; ++c)
								{
									const float v = HalfToFloat(texel[c]);
									const float g = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
									rgb[c] = static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f);
								}
							}
							else
							{
								const uint8_t* texel = row + x * 4;
								rgb[0] = texel[0]; rgb[1] = texel[1]; rgb[2] = texel[2];
							}
							out.write(reinterpret_cast<const char*>(rgb), 3);
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
		FG_CORE_ERROR("VkRenderer: target capture to '{0}' failed", path);
	return ok;
}

}}
