#include "fgpch.h"
#include "Platform/Vulkan/VkRenderer.h"
#include "Platform/Vulkan/VkRendererInternal.h"
#include "Platform/Vulkan/Pipeline/PipelinePresets.h"

#include "Platform/Vulkan/Frame/Barriers.h"
#include "Platform/Vulkan/Memory/UploadRegions.h"
#include "Platform/Vulkan/Resources/Subresources.h"
#include "Forge/Core/Log.h"
#include "Forge/Renderer/Mesh.h"

#include <vk_mem_alloc.h>

namespace Forge { namespace Vulkan {

namespace {

	// Uniform block payloads, mirroring resources/shaders byte for byte
	// (GL_EXT_scalar_block_layout: each member at its natural offset).

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

	// The uniform payloads must match the shader blocks byte for byte, or
	// the shader reads the wrong members.
	static_assert(sizeof(CameraData) == 208, "CameraData must match pbr.frag CameraData (2*mat4 + 5*vec4)");
	static_assert(sizeof(ObjectData) == 176, "ObjectData must match ObjectData in pbr.vert/pbr.frag (2*mat4 + 3*vec4)");

}  // namespace

// ---------------------------------------------------------------------------
// Render passes
// ---------------------------------------------------------------------------

bool VkRenderer::CreateRenderPasses()
{
	const Device& device = m_Device;

	// HDR target: RGBA16F colour + D32 depth, both cleared, both read back.
	{
		const std::array<VkAttachmentDescription, 2> attachments{
			VkAttachmentDescription{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
				VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
			VkAttachmentDescription{ 0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
				VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
		};
		const std::array<VkAttachmentReference, 1> colorRefs{ VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };
		const VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
		m_RenderPassHdr = CreateRenderPassImpl(device, attachments, colorRefs, &depthRef, VK_FORMAT_R16G16B16A16_SFLOAT);
		if (m_RenderPassHdr == VK_NULL_HANDLE)
			return false;
	}

	// Shadow map: depth only.
	{
		const std::array<VkAttachmentDescription, 1> attachments{
			VkAttachmentDescription{ 0, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
				VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL },
		};
		m_RenderPassShadow = CreateRenderPassImpl(device, attachments, {}, nullptr, VK_FORMAT_UNDEFINED);
		if (m_RenderPassShadow == VK_NULL_HANDLE)
			return false;
	}

	// One RGBA16F colour target: bloom half/A/B and the tonemapped buffer.
	{
		const std::array<VkAttachmentDescription, 1> attachments{
			VkAttachmentDescription{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
				VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		};
		const std::array<VkAttachmentReference, 1> colorRefs{ VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };
		m_RenderPassColor16 = CreateRenderPassImpl(device, attachments, colorRefs, nullptr, VK_FORMAT_R16G16B16A16_SFLOAT);
		if (m_RenderPassColor16 == VK_NULL_HANDLE)
			return false;
	}

	// SSAO output: R8.
	{
		const std::array<VkAttachmentDescription, 1> attachments{
			VkAttachmentDescription{ 0, VK_FORMAT_R8_UNORM, VK_SAMPLE_COUNT_1_BIT,
				VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		};
		const std::array<VkAttachmentReference, 1> colorRefs{ VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };
		m_RenderPassR8 = CreateRenderPassImpl(device, attachments, colorRefs, nullptr, VK_FORMAT_R8_UNORM);
		if (m_RenderPassR8 == VK_NULL_HANDLE)
			return false;
	}

	// Final composite: the swapchain format. The pass leaves the image in
	// COLOR_ATTACHMENT_OPTIMAL - the ImGui backend draws next through dynamic
	// rendering, and EndFrame is what takes the image to PRESENT_SRC.
	{
		const std::array<VkAttachmentDescription, 1> attachments{
			VkAttachmentDescription{ 0, m_Swapchain.GetFormat().format, VK_SAMPLE_COUNT_1_BIT,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_STORE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL },
		};
		const std::array<VkAttachmentReference, 1> colorRefs{ VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };
		m_RenderPassPresent = CreateRenderPassImpl(device, attachments, colorRefs, nullptr, m_Swapchain.GetFormat().format);
		if (m_RenderPassPresent == VK_NULL_HANDLE)
			return false;
	}

	// IBL cubemap faces (irradiance / prefilter output).
	{
		const std::array<VkAttachmentDescription, 1> attachments{
			VkAttachmentDescription{ 0, VK_FORMAT_R16G16B16A16_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
				VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE,
				VK_ATTACHMENT_LOAD_OP_DONT_CARE, VK_ATTACHMENT_STORE_OP_DONT_CARE,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },
		};
		const std::array<VkAttachmentReference, 1> colorRefs{ VkAttachmentReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL } };
		m_RenderPassCubeFace = CreateRenderPassImpl(device, attachments, colorRefs, nullptr, VK_FORMAT_R16G16B16A16_SFLOAT);
		if (m_RenderPassCubeFace == VK_NULL_HANDLE)
			return false;
	}

	return true;
}

void VkRenderer::DestroyRenderPasses()
{
	const auto& functions = m_Device.GetFunctions();
	if (m_Device.IsNull())
		return;
	auto destroy = [&functions, this](::VkRenderPass& pass)
	{
		if (pass != VK_NULL_HANDLE)
			functions.DestroyRenderPass(m_Device.Handle(), pass, nullptr);
		pass = VK_NULL_HANDLE;
	};
	destroy(m_RenderPassHdr);
	destroy(m_RenderPassShadow);
	destroy(m_RenderPassColor16);
	destroy(m_RenderPassR8);
	destroy(m_RenderPassPresent);
	destroy(m_RenderPassCubeFace);
}

// ---------------------------------------------------------------------------
// Samplers and 1x1 fallbacks
// ---------------------------------------------------------------------------

bool VkRenderer::CreateSamplersAndFallbacks()
{
	Device& device = m_Device;

	m_SamplerAlbedo = CreateSamplerImpl(device, Presets::Sampler::LinearClampToEdge, "Sampler.Albedo");
	m_SamplerShadow = CreateSamplerImpl(device, []() {
		VkSamplerCreateInfo s = Presets::Sampler::LinearClampToEdge;
		s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		return s;
	}(), "Sampler.Shadow");
	m_SamplerLinear = CreateSamplerImpl(device, Presets::Sampler::LinearRepeat, "Sampler.Linear");
	m_SamplerBorderBlack = CreateSamplerImpl(device, []() {
		VkSamplerCreateInfo s = Presets::Sampler::LinearClampToEdge;
		s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
		return s;
	}(), "Sampler.BorderBlack");
	m_SamplerBorderTransparent = CreateSamplerImpl(device, []() {
		VkSamplerCreateInfo s = Presets::Sampler::LinearClampToEdge;
		s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
		return s;
	}(), "Sampler.BorderTransparent");
	m_SamplerBorderWhite = CreateSamplerImpl(device, []() {
		VkSamplerCreateInfo s = Presets::Sampler::LinearClampToEdge;
		s.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
		s.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		return s;
	}(), "Sampler.BorderWhite");
	if (!m_SamplerAlbedo || !m_SamplerShadow || !m_SamplerLinear ||
		!m_SamplerBorderBlack || !m_SamplerBorderTransparent || !m_SamplerBorderWhite)
		return false;

	// 1x1 white RGBA8 albedo fallback.
	m_WhiteImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
		VkExtent3D{ 1, 1, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, "Fallback.White", m_WhiteAlloc);
	if (m_WhiteImage == VK_NULL_HANDLE)
		return false;
	m_WhiteView = CreateViewImpl(device, m_WhiteImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);

	// 1x1 RGBA16F cube fallbacks for the environment and irradiance bindings.
	auto makeCubeFallback = [this](const char* name, ::VkImage& image, ::VkImageView& view, VmaAllocation& alloc)
	{
		image = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
			VkExtent3D{ 1, 1, 1 }, 1, 6, VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
			VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, name, alloc);
		if (image == VK_NULL_HANDLE)
			return false;
		view = CreateViewImpl(m_Device, image, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 6, VK_IMAGE_VIEW_TYPE_CUBE);
		return view != VK_NULL_HANDLE;
	};
	if (!makeCubeFallback("Fallback.EnvCube", m_EnvFallbackImage, m_EnvFallbackView, m_EnvFallbackAlloc) ||
		!makeCubeFallback("Fallback.IrradianceCube", m_IrradianceFallbackImage, m_IrradianceFallbackView, m_IrradianceFallbackAlloc))
		return false;

	// Fill the fallbacks (one staged texel each) through the staging pool.
	const uint8_t white[4] = { 255, 255, 255, 255 };
	{
		Ref<Buffer> staging = m_Staging->AddStagingBuffer(white, sizeof(white));
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
		RecordImageBarrier(m_Device, cmd, imageBarrier, m_WhiteImage);

		const VkBufferImageCopy region{};
		VkBufferImageCopy copy{};
		copy.imageSubresource = Subresources::FullLayers(VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1);
		copy.imageExtent = VkExtent3D{ 1, 1, 1 };
		(void)region;
		m_Device.GetFunctions().CmdCopyBufferToImage(cmd, staging->Handle(), m_WhiteImage,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

		ImageBarrierBuilder toRead{};
		toRead.SubresourceRange = imageBarrier.SubresourceRange;
		toRead.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toRead.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toRead.SrcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
		toRead.DstAccess = VK_ACCESS_SHADER_READ_BIT;
		toRead.SrcStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
		toRead.DstStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		RecordImageBarrier(m_Device, cmd, toRead, m_WhiteImage);

		m_Device.GetFunctions().EndCommandBuffer(cmd);
	}

	auto fillCubeFallback = [this](::VkImage image)
	{
		// One zero texel per face (six contiguous texels of RGBA16F).
		static const uint16_t zeros[6 * 4] = {};
		Ref<Buffer> staging = m_Staging->AddStagingBuffer(zeros, sizeof(zeros));
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
		imageBarrier.SubresourceRange = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, 1, 6);
		imageBarrier.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageBarrier.NewLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		imageBarrier.DstAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
		RecordImageBarrier(m_Device, cmd, imageBarrier, image);

		VkBufferImageCopy copy{};
		copy.imageSubresource = Subresources::FullLayers(VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 6);
		copy.imageExtent = VkExtent3D{ 1, 1, 1 };
		m_Device.GetFunctions().CmdCopyBufferToImage(cmd, staging->Handle(), image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

		ImageBarrierBuilder toRead{};
		toRead.SubresourceRange = imageBarrier.SubresourceRange;
		toRead.OldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toRead.NewLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toRead.SrcAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
		toRead.DstAccess = VK_ACCESS_SHADER_READ_BIT;
		toRead.SrcStages = VK_PIPELINE_STAGE_TRANSFER_BIT;
		toRead.DstStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		RecordImageBarrier(m_Device, cmd, toRead, image);

		m_Device.GetFunctions().EndCommandBuffer(cmd);
		return true;
	};

	if (!fillCubeFallback(m_EnvFallbackImage) || !fillCubeFallback(m_IrradianceFallbackImage))
		return false;

	// Uploads are flushed now so the fallbacks are ready before frame one.
	(void)m_Staging->Submit();
	(void)m_Staging->Wait();
	return true;
}

void VkRenderer::DestroySamplersAndFallbacks()
{
	const auto& functions = m_Device.GetFunctions();
	if (m_Device.IsNull())
		return;
	auto destroyImage = [this](::VkImage& image, ::VkImageView& view, VmaAllocation& alloc)
	{
		if (view != VK_NULL_HANDLE)
			m_Device.GetFunctions().DestroyImageView(m_Device.Handle(), view, nullptr);
		view = VK_NULL_HANDLE;
		if (image != VK_NULL_HANDLE && m_Allocator)
		{
			ImageAllocationInfo allocation{};
			allocation.Image = image;
			allocation.Allocation.Handle = alloc;
			allocation.Allocation.Allocator = m_Allocator.get();
			(void)m_Allocator->FreeAllocation(allocation);
		}
		image = VK_NULL_HANDLE;
		alloc = nullptr;
	};
	destroyImage(m_WhiteImage, m_WhiteView, m_WhiteAlloc);
	destroyImage(m_EnvFallbackImage, m_EnvFallbackView, m_EnvFallbackAlloc);
	destroyImage(m_IrradianceFallbackImage, m_IrradianceFallbackView, m_IrradianceFallbackAlloc);

	auto destroySampler = [&functions, this](::VkSampler& sampler)
	{
		if (sampler != VK_NULL_HANDLE)
			functions.DestroySampler(m_Device.Handle(), sampler, nullptr);
		sampler = VK_NULL_HANDLE;
	};
	destroySampler(m_SamplerAlbedo);
	destroySampler(m_SamplerShadow);
	destroySampler(m_SamplerLinear);
	destroySampler(m_SamplerBorderBlack);
	destroySampler(m_SamplerBorderTransparent);
	destroySampler(m_SamplerBorderWhite);
}

// ---------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------

bool VkRenderer::CreateTargets(uint32_t presentWidth, uint32_t presentHeight, uint32_t renderWidth, uint32_t renderHeight)
{
	if (!m_DeviceReady || renderWidth == 0 || renderHeight == 0)
		return false;

	DestroyTargets();

	m_PresentWidth = presentWidth;
	m_PresentHeight = presentHeight;
	m_RenderWidth = renderWidth;
	m_RenderHeight = renderHeight;

	const uint32_t halfWidth = std::max(1u, renderWidth / 2u);
	const uint32_t halfHeight = std::max(1u, renderHeight / 2u);

	m_HdrImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
		VkExtent3D{ renderWidth, renderHeight, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, "HDR", m_HdrAlloc);
	m_DepthImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
		VkExtent3D{ renderWidth, renderHeight, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, "HDR.Depth", m_DepthAlloc);
	m_SsaoImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R8_UNORM,
		VkExtent3D{ renderWidth, renderHeight, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, "SSAO", m_SsaoAlloc);
	m_TonemapImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
		VkExtent3D{ presentWidth, presentHeight, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, "Tonemapped", m_TonemapAlloc);
	m_BloomHalfImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
		VkExtent3D{ halfWidth, halfHeight, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, "BloomHalf", m_BloomHalfAlloc);
	m_BloomAImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
		VkExtent3D{ halfWidth, halfHeight, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, "BloomA", m_BloomAAlloc);
	m_BloomBImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_R16G16B16A16_SFLOAT,
		VkExtent3D{ halfWidth, halfHeight, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, "BloomB", m_BloomBAlloc);
	m_ShadowImage = CreateImageImpl(*m_Allocator, VK_IMAGE_TYPE_2D, VK_FORMAT_D32_SFLOAT,
		VkExtent3D{ m_ShadowMapSize, m_ShadowMapSize, 1 }, 1, 1, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 0, "ShadowMap", m_ShadowAlloc);

	if (!m_HdrImage || !m_DepthImage || !m_SsaoImage || !m_TonemapImage ||
		!m_BloomHalfImage || !m_BloomAImage || !m_BloomBImage || !m_ShadowImage)
	{
		FG_CORE_ERROR("VkRenderer: offscreen target allocation failed");
		return false;
	}

	m_HdrView = CreateViewImpl(m_Device, m_HdrImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);
	m_DepthView = CreateViewImpl(m_Device, m_DepthImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);
	m_SsaoView = CreateViewImpl(m_Device, m_SsaoImage, VK_FORMAT_R8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);
	m_TonemapView = CreateViewImpl(m_Device, m_TonemapImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);
	m_BloomHalfView = CreateViewImpl(m_Device, m_BloomHalfImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);
	m_BloomAView = CreateViewImpl(m_Device, m_BloomAImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);
	m_BloomBView = CreateViewImpl(m_Device, m_BloomBImage, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);
	m_ShadowView = CreateViewImpl(m_Device, m_ShadowImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);

	// Uniform buffers (host-visible, mapped for life).
	const size_t objectBytes = ObjectSlotBytes * ObjectRingSize;
	m_CameraUbo = CreateHostBuffer(*m_Allocator, sizeof(CameraData), "Ubo.Camera", m_CameraUboAlloc, &m_CameraUboData);
	m_ShadowParamsUbo = CreateHostBuffer(*m_Allocator, 2 * sizeof(glm::vec2), "Ubo.ShadowParams", m_ShadowParamsUboAlloc, &m_ShadowParamsUboData);
	m_ShadowCameraUbo = CreateHostBuffer(*m_Allocator, sizeof(glm::mat4), "Ubo.ShadowCamera", m_ShadowCameraUboAlloc, &m_ShadowCameraUboData);
	m_SsaoUbo = CreateHostBuffer(*m_Allocator, 96, "Ubo.SSAO", m_SsaoUboAlloc, &m_SsaoUboData);
	m_BloomUbo = CreateHostBuffer(*m_Allocator, 32, "Ubo.Bloom", m_BloomUboAlloc, &m_BloomUboData);
	m_BlurUbo = CreateHostBuffer(*m_Allocator, 32, "Ubo.Blur", m_BlurUboAlloc, &m_BlurUboData);
	m_HdrUbo = CreateHostBuffer(*m_Allocator, 16, "Ubo.HDR", m_HdrUboAlloc, &m_HdrUboData);
	m_FinalUbo = CreateHostBuffer(*m_Allocator, 32, "Ubo.Final", m_FinalUboAlloc, &m_FinalUboData);
	m_IrradianceUbo = CreateHostBuffer(*m_Allocator, 32, "Ubo.Irradiance", m_IrradianceUboAlloc, &m_IrradianceUboData);
	m_PrefilterUbo = CreateHostBuffer(*m_Allocator, 32, "Ubo.Prefilter", m_PrefilterUboAlloc, &m_PrefilterUboData);
	m_ObjectUbo = CreateHostBuffer(*m_Allocator, objectBytes, "Ubo.Object", m_ObjectUboAlloc, &m_ObjectUboData);

	if (!m_CameraUbo || !m_ShadowParamsUbo || !m_ShadowCameraUbo || !m_SsaoUbo || !m_BloomUbo ||
		!m_BlurUbo || !m_HdrUbo || !m_FinalUbo || !m_ObjectUbo)
	{
		FG_CORE_ERROR("VkRenderer: uniform buffer allocation failed");
		return false;
	}

	// Framebuffers.
	{
		const std::array<::VkImageView, 2> views{ m_HdrView, m_DepthView };
		m_FbHdr = CreateFramebufferImpl(m_Device, m_RenderPassHdr, views, renderWidth, renderHeight);
	}
	m_FbSsao = CreateFramebufferImpl(m_Device, m_RenderPassR8, std::span<const ::VkImageView>(&m_SsaoView, 1), renderWidth, renderHeight);
	m_FbTonemap = CreateFramebufferImpl(m_Device, m_RenderPassColor16, std::span<const ::VkImageView>(&m_TonemapView, 1), presentWidth, presentHeight);
	m_FbBloomHalf = CreateFramebufferImpl(m_Device, m_RenderPassColor16, std::span<const ::VkImageView>(&m_BloomHalfView, 1), halfWidth, halfHeight);
	m_FbBloomA = CreateFramebufferImpl(m_Device, m_RenderPassColor16, std::span<const ::VkImageView>(&m_BloomAView, 1), halfWidth, halfHeight);
	m_FbBloomB = CreateFramebufferImpl(m_Device, m_RenderPassColor16, std::span<const ::VkImageView>(&m_BloomBView, 1), halfWidth, halfHeight);
	m_FbShadow = CreateFramebufferImpl(m_Device, m_RenderPassShadow, std::span<const ::VkImageView>(&m_ShadowView, 1), m_ShadowMapSize, m_ShadowMapSize);

	if (!m_FbHdr || !m_FbSsao || !m_FbTonemap || !m_FbBloomHalf || !m_FbBloomA || !m_FbBloomB || !m_FbShadow)
	{
		FG_CORE_ERROR("VkRenderer: framebuffer creation failed");
		return false;
	}

	if (!CreateSwapchainFramebuffers())
		return false;

	FG_CORE_INFO("VkRenderer: targets {0}x{1} (present {2}x{3}, bloom {4}x{5}, shadow {6})",
		renderWidth, renderHeight, presentWidth, presentHeight, halfWidth, halfHeight, m_ShadowMapSize);
	return true;
}

void VkRenderer::DestroyTargets()
{
	const auto& functions = m_Device.GetFunctions();
	if (m_Device.IsNull() && !m_Allocator)
		return;

	DestroySwapchainFramebuffers();

	auto destroyFb = [&functions, this](::VkFramebuffer& fb)
	{
		if (fb != VK_NULL_HANDLE)
			functions.DestroyFramebuffer(m_Device.Handle(), fb, nullptr);
		fb = VK_NULL_HANDLE;
	};
	destroyFb(m_FbHdr);
	destroyFb(m_FbSsao);
	destroyFb(m_FbTonemap);
	destroyFb(m_FbBloomHalf);
	destroyFb(m_FbBloomA);
	destroyFb(m_FbBloomB);
	destroyFb(m_FbShadow);

	auto destroyHost = [this](::VkBuffer& buffer, VmaAllocation& alloc, void*& data)
	{
		if (alloc == nullptr || !m_Allocator)
		{
			buffer = VK_NULL_HANDLE;
			data = nullptr;
			return;
		}
		// Unmap through the allocator's bookkeeping, then free.
		AllocationMapInfo mapInfo{};
		mapInfo.Allocation.Handle = alloc;
		m_Allocator->UnmapMemory(mapInfo);

		BufferAllocationInfo allocation{}
		;
		allocation.Buffer = buffer;
		allocation.Allocation.Handle = alloc;
		allocation.Allocation.Allocator = m_Allocator.get();
		if (!m_Allocator->FreeAllocation(allocation))
			FG_CORE_WARN("VkRenderer: uniform buffer free refused");
		buffer = VK_NULL_HANDLE;
		alloc = nullptr;
		data = nullptr;
	};
	destroyHost(m_CameraUbo, m_CameraUboAlloc, m_CameraUboData);
	destroyHost(m_ShadowParamsUbo, m_ShadowParamsUboAlloc, m_ShadowParamsUboData);
	destroyHost(m_ShadowCameraUbo, m_ShadowCameraUboAlloc, m_ShadowCameraUboData);
	destroyHost(m_SsaoUbo, m_SsaoUboAlloc, m_SsaoUboData);
	destroyHost(m_BloomUbo, m_BloomUboAlloc, m_BloomUboData);
	destroyHost(m_BlurUbo, m_BlurUboAlloc, m_BlurUboData);
	destroyHost(m_HdrUbo, m_HdrUboAlloc, m_HdrUboData);
	destroyHost(m_FinalUbo, m_FinalUboAlloc, m_FinalUboData);
	destroyHost(m_IrradianceUbo, m_IrradianceUboAlloc, m_IrradianceUboData);
	destroyHost(m_PrefilterUbo, m_PrefilterUboAlloc, m_PrefilterUboData);
	destroyHost(m_ObjectUbo, m_ObjectUboAlloc, m_ObjectUboData);

	auto destroyImage = [this](::VkImage& image, ::VkImageView& view, VmaAllocation& alloc)
	{
		if (view != VK_NULL_HANDLE)
			m_Device.GetFunctions().DestroyImageView(m_Device.Handle(), view, nullptr);
		view = VK_NULL_HANDLE;
		if (image != VK_NULL_HANDLE && m_Allocator)
		{
			ImageAllocationInfo allocation{};
			allocation.Image = image;
			allocation.Allocation.Handle = alloc;
			allocation.Allocation.Allocator = m_Allocator.get();
			(void)m_Allocator->FreeAllocation(allocation);
		}
		image = VK_NULL_HANDLE;
		alloc = nullptr;
	};
	destroyImage(m_HdrImage, m_HdrView, m_HdrAlloc);
	destroyImage(m_DepthImage, m_DepthView, m_DepthAlloc);
	destroyImage(m_SsaoImage, m_SsaoView, m_SsaoAlloc);
	destroyImage(m_TonemapImage, m_TonemapView, m_TonemapAlloc);
	destroyImage(m_BloomHalfImage, m_BloomHalfView, m_BloomHalfAlloc);
	destroyImage(m_BloomAImage, m_BloomAView, m_BloomAAlloc);
	destroyImage(m_BloomBImage, m_BloomBView, m_BloomBAlloc);
	destroyImage(m_ShadowImage, m_ShadowView, m_ShadowAlloc);

	// IBL / probe cubemaps follow the targets they are sized with.
	destroyImage(m_ReflectionImage, m_ReflectionView, m_ReflectionAlloc);
	destroyImage(m_ReflectionDepthImage, m_ReflectionDepthView, m_ReflectionDepthAlloc);
	for (::VkImageView& view : m_ReflectionFaceViews)
		if (view != VK_NULL_HANDLE)
			m_Device.GetFunctions().DestroyImageView(m_Device.Handle(), view, nullptr);
	m_ReflectionFaceViews.clear();
	destroyImage(m_EnvImage, m_EnvImageView, m_EnvAlloc);
	destroyImage(m_IrradianceImage, m_IrradianceView, m_IrradianceAlloc);
	destroyImage(m_PrefilterImage, m_PrefilterView, m_PrefilterAlloc);
	for (::VkFramebuffer& fb : m_ReflectionFaceFramebuffers)
		if (fb != VK_NULL_HANDLE)
			m_Device.GetFunctions().DestroyFramebuffer(m_Device.Handle(), fb, nullptr);
	m_ReflectionFaceFramebuffers.clear();
	for (::VkFramebuffer& fb : m_IrradianceFaceFramebuffers)
		if (fb != VK_NULL_HANDLE)
			m_Device.GetFunctions().DestroyFramebuffer(m_Device.Handle(), fb, nullptr);
	m_IrradianceFaceFramebuffers.clear();
	for (::VkFramebuffer& fb : m_PrefilterFaceFramebuffers)
		if (fb != VK_NULL_HANDLE)
			m_Device.GetFunctions().DestroyFramebuffer(m_Device.Handle(), fb, nullptr);
	m_PrefilterFaceFramebuffers.clear();
}

bool VkRenderer::CreateSwapchainFramebuffers()
{
	DestroySwapchainFramebuffers();
	m_SwapchainViews.resize(m_Swapchain.GetImages().size());
	m_SwapchainFramebuffers.resize(m_Swapchain.GetImages().size());
	for (size_t i = 0; i < m_Swapchain.GetImages().size(); ++i)
	{
		m_SwapchainViews[i] = CreateViewImpl(m_Device, m_Swapchain.GetImages()[i],
			m_Swapchain.GetFormat().format, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_VIEW_TYPE_2D);
		if (m_SwapchainViews[i] == VK_NULL_HANDLE)
			return false;
		m_SwapchainFramebuffers[i] = CreateFramebufferImpl(m_Device, m_RenderPassPresent,
			std::span<const ::VkImageView>(&m_SwapchainViews[i], 1),
			m_Swapchain.GetExtent().width, m_Swapchain.GetExtent().height);
		if (m_SwapchainFramebuffers[i] == VK_NULL_HANDLE)
			return false;
	}
	return true;
}

void VkRenderer::DestroySwapchainFramebuffers()
{
	for (::VkFramebuffer& fb : m_SwapchainFramebuffers)
		if (fb != VK_NULL_HANDLE && !m_Device.IsNull())
			m_Device.GetFunctions().DestroyFramebuffer(m_Device.Handle(), fb, nullptr);
	m_SwapchainFramebuffers.clear();
	for (::VkImageView& view : m_SwapchainViews)
		if (view != VK_NULL_HANDLE && !m_Device.IsNull())
			m_Device.GetFunctions().DestroyImageView(m_Device.Handle(), view, nullptr);
	m_SwapchainViews.clear();
}

// ---------------------------------------------------------------------------
// Fullscreen quad
// ---------------------------------------------------------------------------

bool VkRenderer::CreateFullscreenQuad()
{
	// The quad is uploaded through the staging pool on first use; the
	// buffers are device-local.
	VkBufferCreateInfo vertexInfo{};
	vertexInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexInfo.size = 4 * 4 * sizeof(float);
	vertexInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	vertexInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkBufferCreateInfo indexInfo{};
	indexInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexInfo.size = 6 * sizeof(uint32_t);
	indexInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	indexInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	BufferAllocationInfo vertexAlloc{};
	BufferAllocationInfo indexAlloc{};
	AllocationCreateInfo alloc{};
	alloc.Preference = MemoryPreference::PreferGpu;
	alloc.RequiredProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	if (!m_Allocator->AllocateBuffer("Quad.VBO", vertexInfo, alloc, vertexAlloc) ||
		!m_Allocator->AllocateBuffer("Quad.IBO", indexInfo, alloc, indexAlloc))
		return false;
	m_QuadVertexBuffer = vertexAlloc.Buffer;
	m_QuadVertexAlloc = static_cast<VmaAllocation>(vertexAlloc.Allocation.Handle);
	m_QuadIndexBuffer = indexAlloc.Buffer;
	m_QuadIndexAlloc = static_cast<VmaAllocation>(indexAlloc.Allocation.Handle);

	static const float vertices[16] = {
		-1.0f, -1.0f, 0.0f, 0.0f,
		 1.0f, -1.0f, 1.0f, 0.0f,
		 1.0f,  1.0f, 1.0f, 1.0f,
		-1.0f,  1.0f, 0.0f, 1.0f,
	};
	static const uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };

	Ref<Buffer> staging = m_Staging->AddStagingBuffer(vertices, sizeof(vertices));
	if (!staging)
		return false;
	const size_t indexStart = 4 * 4 * sizeof(float);
	Ref<Buffer> stagingIndices = m_Staging->AddStagingBuffer(indices, sizeof(indices));
	if (!stagingIndices)
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
	RecordBufferBarrier(m_Device, cmd, bufferBarrier, stagingIndices->Handle());

	const VkBufferCopy vertexCopy{ 0, 0, sizeof(vertices) };
	m_Device.GetFunctions().CmdCopyBuffer(cmd, staging->Handle(), m_QuadVertexBuffer, 1, &vertexCopy);
	const VkBufferCopy indexCopy{ 0, 0, sizeof(indices) };
	m_Device.GetFunctions().CmdCopyBuffer(cmd, stagingIndices->Handle(), m_QuadIndexBuffer, 1, &indexCopy);
	(void)indexStart;

	m_Device.GetFunctions().EndCommandBuffer(cmd);
	(void)m_Staging->Submit();
	(void)m_Staging->Wait();

#if defined(FG_DEBUG)
	SetDebugName(m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(m_QuadVertexBuffer), "FullscreenQuad.VBO");
	SetDebugName(m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(m_QuadIndexBuffer), "FullscreenQuad.IBO");
#endif
	return true;
}

void VkRenderer::DestroyFullscreenQuad()
{
	if (!m_Allocator)
	{
		m_QuadVertexBuffer = VK_NULL_HANDLE;
		m_QuadIndexBuffer = VK_NULL_HANDLE;
		return;
	}
	auto freeBuffer = [this](::VkBuffer& buffer, VmaAllocation& alloc)
	{
		if (buffer == VK_NULL_HANDLE)
			return;
		BufferAllocationInfo allocation{};
		allocation.Buffer = buffer;
		allocation.Allocation.Handle = alloc;
		allocation.Allocation.Allocator = m_Allocator.get();
		(void)m_Allocator->FreeAllocation(allocation);
		buffer = VK_NULL_HANDLE;
		alloc = nullptr;
	};
	freeBuffer(m_QuadVertexBuffer, m_QuadVertexAlloc);
	freeBuffer(m_QuadIndexBuffer, m_QuadIndexAlloc);
}

// ---------------------------------------------------------------------------
// Descriptor layouts
// ---------------------------------------------------------------------------

bool VkRenderer::CreateDescriptorLayouts()
{
	using B = VkDescriptorSetLayoutBinding;

	// PBR: the full contract of pbr.vert + pbr.frag (bindings 0-11, set 0).
	const std::array<B, 12> pbrBindings{
		B{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 3, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 5, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 6, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 7, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 8, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 9, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 10, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 11, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	if (!m_SetLayoutPbr.Create(m_Device, pbrBindings, "Layout.PBR"))
		return false;

	// Shadow: ObjectData (1) + ShadowData (4), matching shadow.vert.
	const std::array<B, 2> shadowBindings{
		B{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
		B{ 4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
	};
	if (!m_SetLayoutShadow.Create(m_Device, shadowBindings, "Layout.Shadow"))
		return false;

	const std::array<B, 3> ssaoBindings{
		B{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	if (!m_SetLayoutSsao.Create(m_Device, ssaoBindings, "Layout.SSAO"))
		return false;

	const std::array<B, 3> bloomBindings{
		B{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	if (!m_SetLayoutBloom.Create(m_Device, bloomBindings, "Layout.Bloom"))
		return false;

	const std::array<B, 4> hdrBindings{
		B{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	if (!m_SetLayoutTonemap.Create(m_Device, hdrBindings, "Layout.Tonemap"))
		return false;

	const std::array<B, 4> finalBindings{
		B{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 2, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	if (!m_SetLayoutFinal.Create(m_Device, finalBindings, "Layout.Final"))
		return false;

	const std::array<B, 3> irradianceBindings{
		B{ 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 1, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		B{ 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
	};
	if (!m_SetLayoutIrradiance.Create(m_Device, irradianceBindings, "Layout.Irradiance"))
		return false;
	if (!m_SetLayoutPrefilter.Create(m_Device, irradianceBindings, "Layout.Prefilter"))
		return false;

	// The frame pool: sized from the layout censuses, not from a hand-picked
	// number. The reflection probe redraws the whole scene into six faces
	// every frame, so the PBR census is paid (faces + 1) times per mesh
	// entity and the shadow census once more. A pool sized for the camera
	// pass alone runs out mid-frame the moment a probe is enabled, and every
	// mesh whose set then refuses is silently skipped - measured on Tetris,
	// 33 blocks x 7 PBR sets x 5 samplers blew an 800-sampler pool and the
	// scene stopped drawing entirely.
	constexpr uint32_t kMeshDrawBudget = 128;  // mesh entities per frame
	constexpr uint32_t kProbeFaces = 6;
	constexpr uint32_t kPostSets = 16;         // SSAO/Bloom/Tonemap/Final chain with headroom

	uint32_t sampled = 0, sampler = 0, uniform = 0;
	const auto add = [&](const DescriptorCensus& census, uint32_t sets)
	{
		for (const VkDescriptorPoolSize& size : census.Sizes)
		{
			const uint32_t total = size.descriptorCount * sets;
			switch (size.type)
			{
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: sampled += total; break;
			case VK_DESCRIPTOR_TYPE_SAMPLER: sampler += total; break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER: uniform += total; break;
			default: break;
			}
		}
	};
	add(m_SetLayoutPbr.GetCensus(), kMeshDrawBudget * (kProbeFaces + 1));
	add(m_SetLayoutShadow.GetCensus(), kMeshDrawBudget);
	add(m_SetLayoutSsao.GetCensus(), kPostSets);
	add(m_SetLayoutBloom.GetCensus(), kPostSets);
	add(m_SetLayoutTonemap.GetCensus(), kPostSets);
	add(m_SetLayoutFinal.GetCensus(), kPostSets);
	add(m_SetLayoutIrradiance.GetCensus(), kProbeFaces);
	add(m_SetLayoutPrefilter.GetCensus(), kProbeFaces);

	// 25% headroom: a scene with more mesh entities than the budget is
	// reported by the accounting, not silently starved.
	sampled += sampled / 4;
	sampler += sampler / 4;
	uniform += uniform / 4;
	const uint32_t maxSets = (kMeshDrawBudget * (kProbeFaces + 2) + 4 * kPostSets + 2 * kProbeFaces) * 5 / 4;

	const std::array<VkDescriptorPoolSize, 3> poolSizes{
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sampled },
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER, sampler },
		VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniform },
	};
	for (DescriptorPool& pool : m_DescriptorPools)
	{
		if (!pool.Create(m_Device, poolSizes, maxSets, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT))
			return false;
	}

	return true;
}

void VkRenderer::DestroyDescriptorLayouts()
{
	m_SetLayoutPbr.Destroy();
	m_SetLayoutShadow.Destroy();
	m_SetLayoutSsao.Destroy();
	m_SetLayoutBloom.Destroy();
	m_SetLayoutTonemap.Destroy();
	m_SetLayoutFinal.Destroy();
	m_SetLayoutIrradiance.Destroy();
	m_SetLayoutPrefilter.Destroy();
	for (DescriptorPool& pool : m_DescriptorPools)
		pool.Destroy();
}

}}
