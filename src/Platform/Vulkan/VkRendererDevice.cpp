#include "fgpch.h"
#include "Platform/Vulkan/VkRenderer.h"
#include "Platform/Vulkan/Pipeline/PipelinePresets.h"
#include "Platform/Vulkan/VkRendererInternal.h"

#include "Platform/Vulkan/Device/DeviceSelection.h"
#include "Platform/Vulkan/Frame/Barriers.h"
#include "Platform/Vulkan/Resources/Subresources.h"
#include "Forge/Core/FileSystem.h"
#include "Forge/Core/Log.h"
#include "Forge/Renderer/Bloom.h"
#include "Forge/Renderer/Mesh.h"

#include <vk_mem_alloc.h>

#include <cmath>

namespace Forge { namespace Vulkan {


VkRenderer& VkRenderer::Get()
{
	static VkRenderer instance;
	return instance;
}

VkRenderer::~VkRenderer()
{
	ShutdownDevice();
}

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

bool VkRenderer::InitDevice(void* glfwWindow, uint32_t width, uint32_t height)
{
	if (m_DeviceReady)
		return true;
	if (!glfwWindow)
	{
		FG_CORE_ERROR("VkRenderer: InitDevice needs a window");
		return false;
	}

	Instance::CreateInfo instanceInfo;
	instanceInfo.ApplicationName = "Forge";
	if (!m_Instance.Create(instanceInfo))
		return false;

	if (!m_Instance.CreateSurface(static_cast<GLFWwindow*>(glfwWindow), m_Surface))
	{
		m_Instance.Destroy();
		return false;
	}

	// Pick the first physical device that can draw and present to this
	// surface. (The Phase 1 selection rules rank queues and memory types;
	// device choice itself stays simple and honest.)
	PhysicalDevice* chosen = nullptr;
	uint32_t graphicsFamily = 0;
	uint32_t presentFamily = 0;
	for (auto& candidate : m_Instance.GetPhysicalDevices())
	{
		if (!candidate->SupportsExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
			continue;

		const int graphics = candidate->FindQueueFamily(VK_QUEUE_GRAPHICS_BIT);
		if (graphics < 0)
			continue;

		bool presents = false;
		for (uint32_t family = 0; family < candidate->GetQueueFamilyProperties().size(); ++family)
		{
			if (candidate->SupportsPresentation(family, m_Surface))
			{
				presents = true;
				presentFamily = family;
				break;
			}
		}
		if (!presents)
			continue;

		chosen = candidate.get();
		graphicsFamily = static_cast<uint32_t>(graphics);
		break;
	}

	if (!chosen)
	{
		FG_CORE_ERROR("VkRenderer: no physical device can draw and present to this surface");
		ShutdownDevice();
		return false;
	}

	m_PhysicalDevicePtr = chosen;
	m_GraphicsFamily = graphicsFamily;
	m_PresentFamily = presentFamily;

	// A dedicated transfer family keeps uploads off the graphics queue when
	// the driver offers one (the Phase 1 "fewest extra capabilities" rule).
	const int transferFamily = chosen->FindQueueFamily(VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT);
	m_TransferFamily = transferFamily >= 0 ? static_cast<uint32_t>(transferFamily) : graphicsFamily;

	Device::CreateInfo deviceInfo;
	deviceInfo.Extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
	{
		Device::QueueRequest graphics;
		graphics.FamilyIndex = graphicsFamily;
		graphics.Priorities = { 1.0f };
		deviceInfo.Queues.push_back(graphics);
		if (m_TransferFamily != graphicsFamily)
		{
			Device::QueueRequest transfer;
			transfer.FamilyIndex = m_TransferFamily;
			transfer.Priorities = { 0.5f };
			deviceInfo.Queues.push_back(transfer);
		}
	}

	// The shaders declare GL_EXT_scalar_block_layout; the feature must be
	// enabled for the module to compile on the driver.
	VkPhysicalDeviceScalarBlockLayoutFeatures scalarFeatures{};
	scalarFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
	scalarFeatures.scalarBlockLayout = VK_TRUE;
	scalarFeatures.pNext = const_cast<void*>(deviceInfo.Features);
	deviceInfo.Features = &scalarFeatures;
	deviceInfo.EnableDeviceAddress = true;

	if (!m_Device.Create(*chosen, deviceInfo))
	{
		ShutdownDevice();
		return false;
	}

	m_GraphicsQueue = m_Device.GetQueue(graphicsFamily, 0);
	if (!m_GraphicsQueue)
	{
		FG_CORE_ERROR("VkRenderer: graphics queue unavailable");
		ShutdownDevice();
		return false;
	}
	Ref<Queue> uploadQueue = m_TransferFamily == graphicsFamily
		? m_GraphicsQueue
		: m_Device.GetQueue(m_TransferFamily, 0);
	if (!uploadQueue)
		uploadQueue = m_GraphicsQueue;

	m_Allocator = VmaDeviceAllocator::Create(m_Device);
	if (!m_Allocator)
	{
		FG_CORE_ERROR("VkRenderer: VMA allocator creation failed");
		ShutdownDevice();
		return false;
	}

	m_Staging = CreateRef<StagingPool>(*m_Allocator, uploadQueue, 8);

	// Swapchain at the window extent.
	Swapchain::CreateInfo swapInfo;
	swapInfo.Surface = m_Surface;
	swapInfo.Width = width;
	swapInfo.Height = height;
	swapInfo.GraphicsQueueFamily = m_GraphicsFamily;
	swapInfo.PresentQueueFamily = m_PresentFamily;
	if (!m_Swapchain.Create(*chosen, m_Device, swapInfo))
	{
		FG_CORE_ERROR("VkRenderer: swapchain creation failed; presentation disabled");
		ShutdownDevice();
		return false;
	}
	if (!m_Swapchain.InitializeImages(m_Device, m_GraphicsFamily))
	{
		ShutdownDevice();
		return false;
	}

	m_PresentWidth = m_Swapchain.GetExtent().width;
	m_PresentHeight = m_Swapchain.GetExtent().height;

	m_FrameLoop = FrameLoop::Create(m_Device, m_GraphicsQueue, m_Swapchain);
	if (!m_FrameLoop)
	{
		FG_CORE_ERROR("VkRenderer: frame loop creation failed");
		ShutdownDevice();
		return false;
	}

	// Vertex layouts from the CPU structs (Phase 4).
	m_VertexLayoutMesh = VertexLayout::FromFields(
		MakeFieldList(FieldFloat3{}, FieldFloat3{}, FieldFloat2{}, FieldFloat3{}, FieldFloat3{}),
		sizeof(Forge::Vertex));
	m_VertexLayoutQuad = VertexLayout::FromFields(
		MakeFieldList(FieldFloat2{}, FieldFloat2{}), 16);
	if (m_VertexLayoutMesh.AttributeCount == 0 || m_VertexLayoutQuad.AttributeCount == 0)
	{
		FG_CORE_ERROR("VkRenderer: vertex layout generation failed");
		ShutdownDevice();
		return false;
	}

	// Shaders from the resources/shaders SPIR-V contract.
	{
		const std::vector<uint32_t> vert = ReadSpirv("resources/shaders/pbr.vert.spv");
		const std::vector<uint32_t> frag = ReadSpirv("resources/shaders/pbr.frag.spv");
		if (!m_VertexShaderPbr.FromSpirv(m_Device, vert, "PBR.vert") || !m_FragmentShaderPbr.FromSpirv(m_Device, frag, "PBR.frag"))
		{
			FG_CORE_ERROR("VkRenderer: PBR shaders missing; the scene will not be drawn");
		}
	}
	{
		const std::vector<uint32_t> vert = ReadSpirv("resources/shaders/shadow.vert.spv");
		const std::vector<uint32_t> frag = ReadSpirv("resources/shaders/shadow.frag.spv");
		if (!m_VertexShaderShadow.FromSpirv(m_Device, vert, "Shadow.vert") || !m_FragmentShaderShadow.FromSpirv(m_Device, frag, "Shadow.frag"))
			FG_CORE_WARN("VkRenderer: shadow shaders missing; shadows off");
	}
	{
		const std::vector<uint32_t> vert = ReadSpirv("resources/shaders/fullscreen.vert.spv");
		if (!m_VertexShaderFullscreen.FromSpirv(m_Device, vert, "Fullscreen.vert"))
			FG_CORE_WARN("VkRenderer: fullscreen vertex shader missing; post-processing off");
		const auto loadFrag = [this](ShaderModule& module, const char* path, const char* name)
		{
			const std::vector<uint32_t> frag = ReadSpirv(path);
			if (!module.FromSpirv(m_Device, frag, name))
				FG_CORE_WARN("VkRenderer: shader missing '{0}' ({1})", name, path);
		};
		loadFrag(m_FragmentShaderSsao, "resources/shaders/ssao.frag.spv", "SSAO.frag");
		loadFrag(m_FragmentShaderBloom, "resources/shaders/bloom.frag.spv", "Bloom.frag");
		loadFrag(m_FragmentShaderBlur, "resources/shaders/blur.frag.spv", "BloomBlur.frag");
		loadFrag(m_FragmentShaderHdr, "resources/shaders/hdr.frag.spv", "HDR.frag");
		loadFrag(m_FragmentShaderFinal, "resources/shaders/final.frag.spv", "Final.frag");
		loadFrag(m_FragmentShaderIrradiance, "resources/shaders/irradiance.frag.spv", "Irradiance.frag");
		loadFrag(m_FragmentShaderPrefilter, "resources/shaders/prefilter.frag.spv", "Prefilter.frag");
	}

	m_DeviceReady = true;
	FG_CORE_INFO("VkRenderer: raw-Vulkan device ready ('{0}', graphics family {1}, {2} swapchain images)",
		chosen->GetProperties().deviceName, m_GraphicsFamily, m_Swapchain.GetImages().size());
	return true;
}

bool VkRenderer::Initialize(void* glfwWindow, uint32_t presentWidth, uint32_t presentHeight,
	uint32_t renderWidth, uint32_t renderHeight)
{
	if (!InitDevice(glfwWindow, presentWidth, presentHeight))
		return false;
	if (!CreateRenderPasses())
		return false;
	if (!CreateSamplersAndFallbacks())
		return false;
	if (!CreateFullscreenQuad())
		return false;
	if (!CreateDescriptorLayouts())
		return false;
	if (!CreatePipelines())
		return false;
	if (!CreateTargets(presentWidth, presentHeight, renderWidth, renderHeight))
		return false;
	return true;
}

void VkRenderer::ShutdownDevice()
{
	WaitForGpuIdle();

	// The cache is dumped while the device is alive: everything this
	// session compiled is what the next session starts from. With no
	// cache (startup never reached CreatePipelines) there is nothing to
	// say.
	if (!m_PipelineCache.IsNull() && !SavePipelineCache())
		FG_CORE_WARN("VkRenderer: pipeline cache not saved");
	m_PipelineCache.Destroy();

	DestroyUploadCaches();
	DestroyPipelines();
	DestroyDescriptorLayouts();
	DestroySwapchainFramebuffers();
	DestroyTargets();
	DestroyFullscreenQuad();
	DestroySamplersAndFallbacks();
	DestroyRenderPasses();

	m_VertexShaderPbr.Destroy();
	m_FragmentShaderPbr.Destroy();
	m_VertexShaderShadow.Destroy();
	m_FragmentShaderShadow.Destroy();
	m_VertexShaderFullscreen.Destroy();
	m_FragmentShaderSsao.Destroy();
	m_FragmentShaderBloom.Destroy();
	m_FragmentShaderBlur.Destroy();
	m_FragmentShaderHdr.Destroy();
	m_FragmentShaderFinal.Destroy();
	m_FragmentShaderIrradiance.Destroy();
	m_FragmentShaderPrefilter.Destroy();

	if (m_FrameLoop)
	{
		m_FrameLoop->Destroy();
		m_FrameLoop = nullptr;
	}
	if (m_Staging)
	{
		m_Staging->Dispose();
		m_Staging = nullptr;
	}
	m_Swapchain.Destroy();
	m_GraphicsQueue = nullptr;
	m_Allocator = nullptr;
	m_Device.Destroy();
	if (m_Surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(m_Instance.Handle(), m_Surface, nullptr);
	m_Surface = VK_NULL_HANDLE;
	m_Instance.Destroy();
	m_DeviceReady = false;
}

void VkRenderer::WaitForGpuIdle()
{
	if (m_GraphicsQueue)
		(void)m_GraphicsQueue->WaitIdle();
	if (m_Staging)
		(void)m_Staging->Wait();
}

bool VkRenderer::RecreateSwapchain(uint32_t width, uint32_t height)
{
	if (!m_DeviceReady || m_Surface == VK_NULL_HANDLE)
		return false;

	Swapchain rebuilt;
	Swapchain::CreateInfo info;
	info.Surface = m_Surface;
	info.Width = width;
	info.Height = height;
	info.GraphicsQueueFamily = m_GraphicsFamily;
	info.PresentQueueFamily = m_PresentFamily;
	info.OldSwapchain = m_Swapchain.Handle();

	if (m_FrameLoop)
	{
		m_FrameLoop->Destroy();
		m_FrameLoop = nullptr;
	}

	// Rebuild in place: the swapchain object is non-movable, and the frame
	// loop is already gone, so the old handle is free to destroy before the
	// new swapchain is created with it as OldSwapchain.
	m_Swapchain.Destroy();
	if (!m_Swapchain.Create(*m_PhysicalDevicePtr, m_Device, info))
		return false;
	if (!m_Swapchain.InitializeImages(m_Device, m_GraphicsFamily))
		return false;

	m_PresentWidth = m_Swapchain.GetExtent().width;
	m_PresentHeight = m_Swapchain.GetExtent().height;

	m_FrameLoop = FrameLoop::Create(m_Device, m_GraphicsQueue, m_Swapchain);
	return m_FrameLoop != nullptr;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

uint32_t VkRenderer::BeginFrame()
{
	// The frame is inactive until the acquire succeeds: every pass checks
	// this index, so a failed acquire leaves nothing to record into. The
	// command buffer of the previous frame is submitted and not begun -
	// recording into it is undefined behaviour the driver punishes with a
	// crash (measured on NVIDIA at window close).
	m_CurrentImageIndex = UINT32_MAX;
	m_CurrentPoolIndex = UINT32_MAX;

	if (!m_DeviceReady || !m_FrameLoop)
		return UINT32_MAX;

	// Reclaim finished staging work (uploads from the previous frames).
	(void)m_Staging->WaitFor(0);

	uint32_t imageIndex = 0;
	const AcquireOutcome outcome = m_FrameLoop->BeginFrame(imageIndex);
	if (outcome != AcquireOutcome::Ok && outcome != AcquireOutcome::Suboptimal)
		return UINT32_MAX;

	if (imageIndex >= PoolRingSize)
	{
		FG_CORE_ERROR("VkRenderer: swapchain image {0} outside the {1}-slot descriptor pool ring",
			imageIndex, PoolRingSize);
		return UINT32_MAX;
	}

	m_CurrentImageIndex = imageIndex;
	m_CurrentPoolIndex = imageIndex;
	m_ObjectSlot = imageIndex * ObjectSlotsPerFrame;   // this image's region of the object ring
	m_SceneBegun = false;

	// The frame's descriptor sets come from this image's pool. The frame that
	// last used it drew the image acquired above - its fence is what
	// BeginFrame just waited - so resetting it now cannot touch a set any
	// in-flight frame is reading.
	DescriptorPool& pool = m_DescriptorPools[imageIndex];
	if (pool.Handle() != VK_NULL_HANDLE)
		(void)pool.Reset();

	return imageIndex;
}

void VkRenderer::EndScene()
{
	EndActiveRenderPass();
}

void VkRenderer::EndFrame(uint32_t imageIndex)
{
	if (!m_DeviceReady || !m_FrameLoop || imageIndex == UINT32_MAX)
		return;

	EndActiveRenderPass();

	// The present render pass leaves the backbuffer in COLOR_ATTACHMENT (the
	// ImGui overlay draws next); presentation requires PRESENT_SRC. The
	// transition is the frame's last recorded work.
	if (imageIndex < m_Swapchain.GetImages().size())
	{
		ImageBarrierBuilder toPresent{};
		toPresent.SubresourceRange = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
		toPresent.OldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		toPresent.NewLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		toPresent.SrcAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		toPresent.DstAccess = 0;
		toPresent.SrcStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		toPresent.DstStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		RecordImageBarrier(m_Device, CurrentCommandBuffer(), toPresent, m_Swapchain.GetImages()[imageIndex]);
	}

	(void)m_Staging->Submit();  // flush uploads recorded this frame, before the frame's work

	const PresentOutcome outcome = m_FrameLoop->EndFrame(imageIndex);
	m_CurrentImageIndex = UINT32_MAX;   // the frame is closed; passes record nothing after this
	m_CurrentPoolIndex = UINT32_MAX;
	if (outcome == PresentOutcome::RebuildNextFrame)
		FG_CORE_INFO("VkRenderer: present reported OUT_OF_DATE; swapchain rebuild scheduled by the app");
}

void VkRenderer::SetBloomIntensity(float intensity)
{
	s_BloomIntensity = std::clamp(intensity, 0.0f, 1.0f);
}

float VkRenderer::GetBloomIntensity()
{
	return s_BloomIntensity;
}

float VkRenderer::s_BloomIntensity = BloomParams{}.Intensity;

VkCommandBuffer VkRenderer::CurrentCommandBuffer() const
{
	// No frame begun (acquire failed, or EndFrame already ran) means there
	// is no command buffer that may legally be recording: answering the
	// previous frame's buffer is how a failed acquire turned into a driver
	// crash. Passes treat VK_NULL_HANDLE as "skip this pass".
	if (m_CurrentImageIndex == UINT32_MAX)
		return VK_NULL_HANDLE;
	return m_FrameLoop ? m_FrameLoop->CurrentCommandBuffer() : VK_NULL_HANDLE;
}

// ---------------------------------------------------------------------------
// Graphics pipeline builder
// ---------------------------------------------------------------------------

::VkPipeline VkRenderer::CreateGraphicsPipeline(const Device& device, ::VkShaderModule vert, ::VkShaderModule frag,
	const VertexLayout* vertexLayout, ::VkRenderPass renderPass, uint32_t subpassIndex,
	::VkPipelineLayout layout,
	const VkPipelineRasterizationStateCreateInfo& rasterizer,
	const VkPipelineDepthStencilStateCreateInfo& depth,
	const VkPipelineColorBlendStateCreateInfo& blend,
	::VkPipelineCache pipelineCache)
{
	std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
	uint32_t stageCount = 0;
	if (vert != VK_NULL_HANDLE)
		stages[stageCount++] = VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr };
	if (frag != VK_NULL_HANDLE)
		stages[stageCount++] = VkPipelineShaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr };

	VkPipelineVertexInputStateCreateInfo vertexInput = Presets::NoVertexInput;
	VkVertexInputBindingDescription binding{};
	std::array<VkVertexInputAttributeDescription, 16> attributes{};
	if (vertexLayout)
	{
		binding = vertexLayout->Binding;
		for (uint32_t i = 0; i < vertexLayout->AttributeCount; ++i)
			attributes[i] = vertexLayout->Attributes[i];
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &binding;
		vertexInput.vertexAttributeDescriptionCount = vertexLayout->AttributeCount;
		vertexInput.pVertexAttributeDescriptions = attributes.data();
	}

	VkPipelineInputAssemblyStateCreateInfo assembly{};
	assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineMultisampleStateCreateInfo multisample{};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineRasterizationStateCreateInfo raster = rasterizer;
	VkPipelineDepthStencilStateCreateInfo depthState = depth;
	VkPipelineColorBlendStateCreateInfo blendState = blend;

	const std::array<VkDynamicState, 2> dynamicStates{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamic{};
	dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dynamic.pDynamicStates = dynamicStates.data();

	VkGraphicsPipelineCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	info.stageCount = stageCount;
	info.pStages = stages.data();
	info.pVertexInputState = &vertexInput;
	info.pInputAssemblyState = &assembly;
	info.pViewportState = &viewportState;
	info.pRasterizationState = &raster;
	info.pMultisampleState = &multisample;
	info.pDepthStencilState = &depthState;
	info.pColorBlendState = &blendState;
	info.pDynamicState = &dynamic;
	info.layout = layout;
	info.renderPass = renderPass;
	info.subpass = subpassIndex;

	::VkPipeline pipeline = VK_NULL_HANDLE;
	if (device.GetFunctions().CreateGraphicsPipelines(device.Handle(), pipelineCache, 1, &info, nullptr, &pipeline) != VK_SUCCESS)
	{
		FG_CORE_ERROR("VkRenderer: graphics pipeline creation failed");
		return VK_NULL_HANDLE;
	}
	return pipeline;
}

// ---------------------------------------------------------------------------
// Pipeline cache persistence
// ---------------------------------------------------------------------------

bool VkRenderer::SavePipelineCache() const
{
	if (m_PipelineCache.IsNull())
		return false;   // nothing was compiled through a cache

	const std::vector<uint8_t> payload = m_PipelineCache.GetData();
	if (payload.empty())
		return true;    // an empty cache has nothing to persist

	const std::string_view deviceName =
		m_PhysicalDevicePtr ? std::string_view(m_PhysicalDevicePtr->GetProperties().deviceName) : std::string_view();
	const std::filesystem::path path = PipelineCacheFile::CachePathFor(deviceName);
	const std::vector<uint8_t> file = PipelineCacheFile::Build(
		m_PhysicalDevicePtr ? PipelineCacheFile::UuidOf(m_PhysicalDevicePtr->GetProperties())
		                    : std::span<const uint8_t, 16>(std::array<uint8_t, 16>{}),
		payload);
	if (!FileSystem::WriteFile(path, file.data(), file.size()))
	{
		FG_CORE_ERROR("VkRenderer: pipeline cache '{0}' could not be written", path.string());
		return false;
	}
	FG_CORE_INFO("VkRenderer: pipeline cache saved ({0} bytes to {1})", payload.size(), path.string());
	return true;
}

}}
