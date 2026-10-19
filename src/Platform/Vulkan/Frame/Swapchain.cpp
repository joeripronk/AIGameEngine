#include "fgpch.h"
#include "Platform/Vulkan/Frame/Swapchain.h"

#include "Platform/Vulkan/Device/VkDevice.h"
#include "Platform/Vulkan/Device/VkQueue.h"
#include "Platform/Vulkan/Device/VkPhysicalDevice.h"
#include "Platform/Vulkan/Frame/Barriers.h"
#include "Platform/Vulkan/Resources/Subresources.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

Swapchain::~Swapchain()
{
	Destroy();
}

VkImageAspectFlags Swapchain::GetDepthAspect() const
{
	return FormatAspectMask(m_Format.format);
}

bool Swapchain::Create(const PhysicalDevice& physicalDevice, Device& device, const CreateInfo& createInfo)
{
	if (createInfo.Surface == VK_NULL_HANDLE)
	{
		FG_CORE_ERROR("Swapchain::Create with no surface");
		return false;
	}
	if (!physicalDevice.SupportsExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME))
	{
		FG_CORE_ERROR("Swapchain::Create: physical device does not support VK_KHR_swapchain");
		return false;
	}

	VkSurfaceCapabilitiesKHR capabilities{};
	if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice.Handle(), createInfo.Surface, &capabilities) != VK_SUCCESS)
	{
		FG_CORE_ERROR("Swapchain::Create: surface capabilities query failed");
		return false;
	}

	uint32_t formatCount = 0;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice.Handle(), createInfo.Surface, &formatCount, nullptr);
	std::vector<VkSurfaceFormatKHR> formats(formatCount);
	if (formatCount > 0)
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice.Handle(), createInfo.Surface, &formatCount, formats.data());

	uint32_t modeCount = 0;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice.Handle(), createInfo.Surface, &modeCount, nullptr);
	std::vector<VkPresentModeKHR> modes(modeCount);
	if (modeCount > 0)
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice.Handle(), createInfo.Surface, &modeCount, modes.data());

	m_Format = SelectSurfaceFormat(std::span<const VkSurfaceFormatKHR>(formats.data(), formats.size()), createInfo.PreferredFormat);
	m_PresentMode = SelectPresentMode(std::span<const VkPresentModeKHR>(modes.data(), modes.size()), createInfo.PreferredPresentMode);
	const uint32_t imageCount = ComputeMinImageCount(m_PresentMode, capabilities.minImageCount, capabilities.maxImageCount);

	// 0 means "inherit the current extent" - the classic rule for surfaces
	// whose size the window manager drives.
	if (capabilities.currentExtent.width != UINT32_MAX)
	{
		m_Extent = capabilities.currentExtent;
	}
	else
	{
		m_Extent.width = std::clamp(createInfo.Width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		m_Extent.height = std::clamp(createInfo.Height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
	}

	// PreTransform: identity when the compositor allows it, otherwise the
	// surface's transform - and the swapchain must accept it.
	m_PreTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	if ((capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) == 0)
		m_PreTransform = capabilities.currentTransform;

	const uint32_t queueFamilies[2] = { createInfo.GraphicsQueueFamily, createInfo.PresentQueueFamily };

	VkSwapchainCreateInfoKHR info{};
	info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	info.surface = createInfo.Surface;
	info.minImageCount = imageCount;
	info.imageFormat = m_Format.format;
	info.imageColorSpace = m_Format.colorSpace;
	info.imageExtent = m_Extent;
	info.imageArrayLayers = 1;
	info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	info.preTransform = m_PreTransform;
	info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	info.presentMode = m_PresentMode;
	info.clipped = VK_TRUE;
	info.oldSwapchain = createInfo.OldSwapchain;

	// One family renders and presents: exclusive. When they differ, the
	// images must be shared concurrently between them.
	if (createInfo.GraphicsQueueFamily == createInfo.PresentQueueFamily)
	{
		info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}
	else
	{
		info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		info.queueFamilyIndexCount = 2;
		info.pQueueFamilyIndices = queueFamilies;
	}

	if (device.GetFunctions().CreateSwapchainKHR(device.Handle(), &info, nullptr, &m_Handle) != VK_SUCCESS)
	{
		FG_CORE_ERROR("Swapchain::Create: vkCreateSwapchainKHR failed (requested {0} images at {1}x{2})",
			imageCount, m_Extent.width, m_Extent.height);
		return false;
	}
	m_Device = &device;

	uint32_t count = 0;
	device.GetFunctions().GetSwapchainImagesKHR(device.Handle(), m_Handle, &count, nullptr);
	m_Images.resize(count);
	if (count > 0)
		device.GetFunctions().GetSwapchainImagesKHR(device.Handle(), m_Handle, &count, m_Images.data());

	FG_CORE_INFO("Swapchain: {0} images, {1}x{2}, present mode {3}", count, m_Extent.width, m_Extent.height, (int)m_PresentMode);
	return true;
}

void Swapchain::Destroy()
{
	if (m_Handle != VK_NULL_HANDLE && m_Device)
		m_Device->GetFunctions().DestroySwapchainKHR(m_Device->Handle(), m_Handle, nullptr);
	m_Handle = VK_NULL_HANDLE;
	m_Images.clear();
	m_Device = nullptr;
	m_NeedRebuild = false;
	m_Suboptimal = false;
}

AcquireOutcome Swapchain::AcquireNextImage(uint64_t timeoutNanos, ::VkSemaphore semaphore, uint32_t& outImageIndex)
{
	outImageIndex = 0;
	if (m_Handle == VK_NULL_HANDLE || !m_Device)
		return AcquireOutcome::Error;

	uint32_t index = 0;
	const VkResult result = m_Device->GetFunctions().AcquireNextImageKHR(m_Device->Handle(), m_Handle, timeoutNanos, semaphore, VK_NULL_HANDLE, &index);
	const AcquireOutcome outcome = ClassifyAcquire(result);

	switch (outcome)
	{
	case AcquireOutcome::Ok:
		outImageIndex = index;
		break;
	case AcquireOutcome::Suboptimal:
		outImageIndex = index;
		m_Suboptimal = true;
		break;
	case AcquireOutcome::RebuildNextFrame:
		m_NeedRebuild = true;
		break;
	default:
		FG_CORE_ERROR("Swapchain: acquire failed ({0})", (int)result);
		break;
	}

	return outcome;
}

bool Swapchain::InitializeImages(Device& device, uint32_t queueFamily)
{
	if (m_Images.empty())
		return true;

	const auto functions = device.GetFunctions();

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = queueFamily;
	::VkCommandPool pool = VK_NULL_HANDLE;
	if (functions.CreateCommandPool(device.Handle(), &poolInfo, nullptr, &pool) != VK_SUCCESS)
	{
		FG_CORE_ERROR("Swapchain: transient command pool creation failed");
		return false;
	}

	VkCommandBufferAllocateInfo allocateInfo{};
	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.commandPool = pool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;
	::VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	if (functions.AllocateCommandBuffers(device.Handle(), &allocateInfo, &commandBuffer) != VK_SUCCESS)
	{
		functions.DestroyCommandPool(device.Handle(), pool, nullptr);
		FG_CORE_ERROR("Swapchain: transient command buffer allocation failed");
		return false;
	}

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (functions.BeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
	{
		functions.FreeCommandBuffers(device.Handle(), pool, 1, &commandBuffer);
		functions.DestroyCommandPool(device.Handle(), pool, nullptr);
		FG_CORE_ERROR("Swapchain: transient command buffer begin failed");
		return false;
	}

	// Every backbuffer starts undefined; PresentSrc is its first defined
	// layout, so the first frame renders to a defined image.
	ImageBarrierBuilder barrier{};
	barrier.SubresourceRange = Subresources::Full(VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);
	barrier.OldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.NewLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	barrier.SrcAccess = 0;
	barrier.DstAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.SrcStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	barrier.DstStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	for (::VkImage image : m_Images)
		RecordImageBarrier(device, commandBuffer, barrier, image);

	if (functions.EndCommandBuffer(commandBuffer) != VK_SUCCESS)
	{
		functions.FreeCommandBuffers(device.Handle(), pool, 1, &commandBuffer);
		functions.DestroyCommandPool(device.Handle(), pool, nullptr);
		FG_CORE_ERROR("Swapchain: transient command buffer end failed");
		return false;
	}

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	// Submit through the queue object so the queue lock is honoured.
	Ref<Queue> queue = device.GetQueue(queueFamily, 0);
	if (!queue || !queue->Submit(std::span<const VkSubmitInfo>(&submitInfo, 1)))
	{
		functions.FreeCommandBuffers(device.Handle(), pool, 1, &commandBuffer);
		functions.DestroyCommandPool(device.Handle(), pool, nullptr);
		FG_CORE_ERROR("Swapchain: backbuffer init submit failed");
		return false;
	}
	if (!queue->WaitIdle())
	{
		functions.FreeCommandBuffers(device.Handle(), pool, 1, &commandBuffer);
		functions.DestroyCommandPool(device.Handle(), pool, nullptr);
		return false;
	}

	functions.FreeCommandBuffers(device.Handle(), pool, 1, &commandBuffer);
	functions.DestroyCommandPool(device.Handle(), pool, nullptr);
	return true;
}

}}
