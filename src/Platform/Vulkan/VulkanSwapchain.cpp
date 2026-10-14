#include "fgpch.h"
#include "Platform/Vulkan/VulkanSwapchain.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <algorithm>

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	VulkanSwapchain::~VulkanSwapchain()
	{
		Destroy();
	}

	bool VulkanSwapchain::Create(void* windowHandle, uint32_t width, uint32_t height)
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		VkInstance instance = VulkanRendererAPI::GetInstance();
		VkSurfaceKHR surface = VulkanRendererAPI::GetSurface();

		if (!device || instance == VK_NULL_HANDLE || surface == VK_NULL_HANDLE || !windowHandle)
		{
			FG_CORE_ERROR("VulkanSwapchain: device/instance/surface not initialized");
			return false;
		}

		VkPhysicalDevice physicalDevice = VulkanRendererAPI::GetPhysicalDevice();
		if (physicalDevice == VK_NULL_HANDLE)
		{
			FG_CORE_ERROR("VulkanSwapchain: no physical device");
			return false;
		}

		m_Width = width;
		m_Height = height;

		// --- Surface capabilities ---
		VkSurfaceCapabilitiesKHR capabilities{};
		VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);
		if (result != VK_SUCCESS)
		{
			FG_CORE_ERROR("VulkanSwapchain: vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed ({0})", (int)result);
			return false;
		}

		// --- Surface formats ---
		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
		if (formats.empty())
		{
			FG_CORE_ERROR("VulkanSwapchain: surface reports no formats");
			return false;
		}

		// --- Present modes ---
		uint32_t presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
		std::vector<VkPresentModeKHR> presentModes(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

		// Prefer an 8-bit BGRA sRGB format for correct gamma on most platforms.
		VkSurfaceFormatKHR chosenFormat = formats[0];
		for (const auto& format : formats)
		{
			if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				chosenFormat = format;
				break;
			}
		}

		// Prefer mailbox (no tearing), then FIFO (always available).
		m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
		if (std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end())
			m_PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;

		// Clamp the extent to what the surface supports (0,0 means "free choice").
		uint32_t extentWidth = width;
		uint32_t extentHeight = height;
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			extentWidth = capabilities.currentExtent.width;
			extentHeight = capabilities.currentExtent.height;
		}
		else
		{
			extentWidth = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			extentHeight = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		}

		uint32_t imageCount = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
			imageCount = capabilities.maxImageCount;

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = chosenFormat.format;
		createInfo.imageColorSpace = chosenFormat.colorSpace;
		createInfo.imageExtent = { extentWidth, extentHeight };
		createInfo.imageArrayLayers = 1;
		// TRANSFER_SRC lets the backends copy from the swapchain image.
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.preTransform = capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = m_PresentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		result = vkCreateSwapchainKHR(VulkanRendererAPI::GetLogicalDevice(), &createInfo, nullptr, &swapchain);
		if (result != VK_SUCCESS)
		{
			FG_CORE_ERROR("VulkanSwapchain: vkCreateSwapchainKHR failed ({0})", (int)result);
			return false;
		}

		m_Swapchain = swapchain;
		m_Width = extentWidth;
		m_Height = extentHeight;
		m_VkFormat = chosenFormat.format;
		m_Format = VulkanRendererAPI::ToNvrhiFormat(chosenFormat.format);

		if (!BuildImagesAndFramebuffers())
		{
			Destroy();
			return false;
		}

		FG_CORE_INFO("Swapchain created: {0}x{1}, {2} images, present mode {3}",
			m_Width, m_Height, (int)m_ImageTextures.size(), (int)m_PresentMode);
		return true;
	}

	bool VulkanSwapchain::BuildImagesAndFramebuffers()
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device || m_Swapchain == VK_NULL_HANDLE)
			return false;

		// Fetch the swapchain's VkImages.
		uint32_t imageCount = 0;
		vkGetSwapchainImagesKHR(VulkanRendererAPI::GetLogicalDevice(), m_Swapchain, &imageCount, nullptr);
		if (imageCount == 0)
		{
			FG_CORE_ERROR("VulkanSwapchain: swapchain has no images");
			return false;
		}

		std::vector<VkImage> images(imageCount);
		vkGetSwapchainImagesKHR(VulkanRendererAPI::GetLogicalDevice(), m_Swapchain, &imageCount, images.data());

		// Wrap each VkImage as an nvrhi texture so the render passes can use it.
		m_ImageTextures.clear();
		m_ImageTextures.reserve(imageCount);

		for (VkImage image : images)
		{
			nvrhi::TextureDesc desc;
			desc.width = m_Width;
			desc.height = m_Height;
			desc.format = m_Format;
			desc.isRenderTarget = true;
			desc.isShaderResource = true;
			desc.debugName = "SwapchainImage";

			nvrhi::TextureHandle texture = device->createHandleForNativeTexture(
				nvrhi::ObjectTypes::VK_Image, (uint64_t)image, desc);

			if (!texture)
			{
				FG_CORE_ERROR("VulkanSwapchain: failed to wrap VkImage as an nvrhi texture");
				return false;
			}

			m_ImageTextures.push_back(texture);
		}

		// One framebuffer per swapchain image.
		m_Framebuffers.clear();
		m_Framebuffers.reserve(imageCount);

		for (auto& texture : m_ImageTextures)
		{
			nvrhi::FramebufferDesc framebufferDesc;
			framebufferDesc.addColorAttachment(texture.Get());

			nvrhi::FramebufferHandle framebuffer = device->createFramebuffer(framebufferDesc);
			if (!framebuffer)
			{
				FG_CORE_ERROR("VulkanSwapchain: failed to create framebuffer for a swapchain image");
				return false;
			}

			m_Framebuffers.push_back(framebuffer);
		}

		return true;
	}

	uint32_t VulkanSwapchain::AcquireNextImage()
	{
		if (m_Swapchain == VK_NULL_HANDLE)
			return UINT32_MAX;

		// nvrhi owns its own queue synchronization. Wait for the GPU to finish
		// the previous frame before blocking on image acquisition, so we never
		// read from an image that is still in use.
		VulkanRendererAPI::WaitForGpuIdle();

		uint32_t imageIndex = 0;
		VkResult result = vkAcquireNextImageKHR(
			VulkanRendererAPI::GetLogicalDevice(), m_Swapchain, UINT64_MAX, VK_NULL_HANDLE, VK_NULL_HANDLE, &imageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			m_SurfaceOutOfDate = true;
			FG_CORE_WARN("Swapchain out of date during acquire");
			return UINT32_MAX;
		}

		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			FG_CORE_ERROR("vkAcquireNextImageKHR failed ({0})", (int)result);
			return UINT32_MAX;
		}

		if (result == VK_SUBOPTIMAL_KHR)
			m_SurfaceOutOfDate = true;

		return imageIndex;
	}

	bool VulkanSwapchain::Present(uint32_t imageIndex)
	{
		if (m_Swapchain == VK_NULL_HANDLE || imageIndex >= m_ImageTextures.size())
			return false;

		// All recorded work must complete before the image is handed to the
		// presentation engine.
		VulkanRendererAPI::WaitForGpuIdle();

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 0;
		presentInfo.pWaitSemaphores = nullptr;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &m_Swapchain;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pResults = nullptr;

		VkResult result = vkQueuePresentKHR(VulkanRendererAPI::GetGraphicsQueue(), &presentInfo);

		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			m_SurfaceOutOfDate = true;
			return false;
		}

		if (result != VK_SUCCESS)
		{
			FG_CORE_ERROR("vkQueuePresentKHR failed ({0})", (int)result);
			return false;
		}

		return !m_SurfaceOutOfDate;
	}

	nvrhi::FramebufferHandle VulkanSwapchain::GetFramebuffer(uint32_t imageIndex) const
	{
		if (imageIndex >= m_Framebuffers.size())
			return nullptr;
		return m_Framebuffers[imageIndex];
	}

	bool VulkanSwapchain::Recreate(uint32_t width, uint32_t height)
	{
		if (m_Swapchain == VK_NULL_HANDLE)
			return false;

		FG_CORE_INFO("Recreating swapchain ({0}x{1})", width, height);

		// The old framebuffers reference the old images, so drop them first.
		m_Framebuffers.clear();
		m_ImageTextures.clear();
		m_SurfaceOutOfDate = false;

		// Destroy the old swapchain and create a fresh one. Any resources that
		// were still in use have already been waited on.
		VkSwapchainKHR oldSwapchain = m_Swapchain;
		m_Swapchain = VK_NULL_HANDLE;

		VkSurfaceCapabilitiesKHR capabilities{};
		VkPhysicalDevice physicalDevice = VulkanRendererAPI::GetPhysicalDevice();
		if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, VulkanRendererAPI::GetSurface(), &capabilities) != VK_SUCCESS)
			return false;

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, VulkanRendererAPI::GetSurface(), &formatCount, nullptr);
		std::vector<VkSurfaceFormatKHR> formats(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, VulkanRendererAPI::GetSurface(), &formatCount, formats.data());
		if (formats.empty())
			return false;

		uint32_t presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, VulkanRendererAPI::GetSurface(), &presentModeCount, nullptr);
		std::vector<VkPresentModeKHR> presentModes(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, VulkanRendererAPI::GetSurface(), &presentModeCount, presentModes.data());
		if (std::find(presentModes.begin(), presentModes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != presentModes.end())
			m_PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		else
			m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;

		uint32_t extentWidth = width;
		uint32_t extentHeight = height;
		if (capabilities.currentExtent.width != UINT32_MAX)
		{
			extentWidth = capabilities.currentExtent.width;
			extentHeight = capabilities.currentExtent.height;
		}
		else
		{
			extentWidth = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
			extentHeight = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
		}

		uint32_t imageCount = capabilities.minImageCount + 1;
		if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
			imageCount = capabilities.maxImageCount;

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = VulkanRendererAPI::GetSurface();
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = m_VkFormat;
		createInfo.imageColorSpace = formats[0].colorSpace;
		createInfo.imageExtent = { extentWidth, extentHeight };
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.preTransform = capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = m_PresentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = oldSwapchain;

		VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
		VkResult result = vkCreateSwapchainKHR(VulkanRendererAPI::GetLogicalDevice(), &createInfo, nullptr, &newSwapchain);
		if (result != VK_SUCCESS)
		{
			FG_CORE_ERROR("Swapchain recreation failed ({0})", (int)result);
			if (oldSwapchain != VK_NULL_HANDLE)
				vkDestroySwapchainKHR(VulkanRendererAPI::GetLogicalDevice(), oldSwapchain, nullptr);
			return false;
		}

		m_Swapchain = newSwapchain;
		m_Width = extentWidth;
		m_Height = extentHeight;

		// The old swapchain can only be destroyed once the new one is created.
		if (oldSwapchain != VK_NULL_HANDLE)
			vkDestroySwapchainKHR(VulkanRendererAPI::GetLogicalDevice(), oldSwapchain, nullptr);

		if (!BuildImagesAndFramebuffers())
		{
			Destroy();
			return false;
		}

		return true;
	}

	void VulkanSwapchain::Destroy()
	{
		m_Framebuffers.clear();
		m_ImageTextures.clear();

		if (m_Swapchain != VK_NULL_HANDLE)
		{
			VkDevice logicalDevice = VulkanRendererAPI::GetLogicalDevice();
			if (logicalDevice != VK_NULL_HANDLE)
				vkDestroySwapchainKHR(logicalDevice, m_Swapchain, nullptr);
			m_Swapchain = VK_NULL_HANDLE;
		}

		m_SurfaceOutOfDate = false;
	}

}

#endif // FG_NO_VULKAN_RUNTIME
