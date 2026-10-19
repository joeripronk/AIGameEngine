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

	// The recreation chain: the new swapchain takes the old one's resources
	// with it, so the old one may only be destroyed once the new one exists.
	// Creating the new one with a handle that has already been destroyed is a
	// handle the driver has no record of, and the driver is entitled to do
	// anything with it.
	::VkSwapchainKHR oldHandle = createInfo.OldSwapchain;
	if (oldHandle == VK_NULL_HANDLE)
		oldHandle = m_Handle;
	info.oldSwapchain = oldHandle;

	::VkSwapchainKHR created = VK_NULL_HANDLE;
	if (device.GetFunctions().CreateSwapchainKHR(device.Handle(), &info, nullptr, &created) != VK_SUCCESS)
	{
		FG_CORE_ERROR("Swapchain::Create: vkCreateSwapchainKHR failed (requested {0} images at {1}x{2})",
			imageCount, m_Extent.width, m_Extent.height);
		return false;
	}

	// The new swapchain holds the old one's resources alive, so the old one can
	// go now.
	if (m_Handle != VK_NULL_HANDLE && m_Handle != created && m_Device)
		m_Device->GetFunctions().DestroySwapchainKHR(m_Device->Handle(), m_Handle, nullptr);
	m_Handle = created;
	m_Device = &device;

	// A fresh swapchain has not gone out of date and is not suboptimal. The
	// flags are what the renderer reads to decide whether to rebuild, so
	// leaving the old ones set makes it rebuild the swapchain it has just
	// built, over and over, for as long as the window is open.
	m_NeedRebuild = false;
	m_Suboptimal = false;
	m_LastAcquireFailure = VK_SUCCESS;

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
	m_LastAcquireFailure = VK_SUCCESS;
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
		m_LastAcquireFailure = VK_SUCCESS;
		break;
	case AcquireOutcome::Suboptimal:
		outImageIndex = index;
		m_Suboptimal = true;
		m_LastAcquireFailure = VK_SUCCESS;
		break;
	case AcquireOutcome::RebuildNextFrame:
		m_NeedRebuild = true;
		break;
	default:
		// Throttled: a frame loop that spins on a dead surface logs per
		// acquire, and a per-frame flood buries the reason in noise.
		// One line per distinct failing result; the counter resets when
		// an acquire succeeds.
		if (result != m_LastAcquireFailure)
		{
			FG_CORE_ERROR("Swapchain: acquire failed ({0})", (int)result);
			m_LastAcquireFailure = result;
		}
		break;
	}

	return outcome;
}

}}
