#include "fgpch.h"
#include "Platform/Vulkan/VulkanSwapchain.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <algorithm>
#include <fstream>

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

	void VulkanSwapchain::SetDepthTexture(nvrhi::TextureHandle depthTexture)	{
		m_DepthTexture = depthTexture;

		// The framebffers bake in their attachments, so rebuild them.
		if (m_Swapchain != VK_NULL_HANDLE && !m_ImageTextures.empty())
			BuildImagesAndFramebuffers();
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
		m_VkImages = images;
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
			// The render passes target these directly, so pin their state and
			// let nvrhi track the transitions from there.
			desc.keepInitialState = true;
			desc.initialState = nvrhi::ResourceStates::RenderTarget;

			nvrhi::TextureHandle texture = device->createHandleForNativeTexture(
				nvrhi::ObjectTypes::VK_Image, (uint64_t)image, desc);

			if (!texture)
			{
				FG_CORE_ERROR("VulkanSwapchain: failed to wrap VkImage as an nvrhi texture");
				return false;
			}

			m_ImageTextures.push_back(texture);
		}

		// One framebuffer per swapchain image. The optional depth texture is
		// shared by all of them and must be supplied by the renderer, whose
		// pipeline declares depth testing.
		m_Framebuffers.clear();
		m_Framebuffers.reserve(imageCount);

		for (auto& texture : m_ImageTextures)
		{
			nvrhi::FramebufferDesc framebufferDesc;
			framebufferDesc.addColorAttachment(texture.Get());
			if (m_DepthTexture)
				framebufferDesc.setDepthAttachment(m_DepthTexture.Get());

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

	bool VulkanSwapchain::CaptureImage(uint32_t imageIndex, const std::string& path) const
	{
		VkDevice logicalDevice = VulkanRendererAPI::GetLogicalDevice();
		if (logicalDevice == VK_NULL_HANDLE || imageIndex >= m_VkImages.size())		{
			FG_CORE_ERROR("VulkanSwapchain::CaptureImage: no swapchain image {0}", imageIndex);
			return false;
		}

		const VkImage image = m_VkImages[imageIndex];
		const uint32_t width = m_Width;
		const uint32_t height = m_Height;

		// The capture buffer holds tightly packed BGRA rows.
		const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width) * height * 4;

		// Find a memory type that is host-visible and host-coherent, so the
		// mapped range can be read directly after the copy completes.
		VkPhysicalDeviceMemoryProperties memoryProperties{};
		vkGetPhysicalDeviceMemoryProperties(VulkanRendererAPI::GetPhysicalDevice(), &memoryProperties);

		uint32_t memoryTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
		{
			const bool hostVisible = (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
			const bool hostCoherent = (memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
			if (hostVisible && hostCoherent)
			{
				memoryTypeIndex = i;
				break;
			}
		}

		if (memoryTypeIndex == UINT32_MAX)
		{
			FG_CORE_ERROR("VulkanSwapchain::CaptureImage: no host-visible coherent memory type");
			return false;
		}

		VkBufferCreateInfo bufferInfo{};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = bufferSize;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkBuffer readbackBuffer = VK_NULL_HANDLE;
		if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &readbackBuffer) != VK_SUCCESS)
		{
			FG_CORE_ERROR("VulkanSwapchain::CaptureImage: vkCreateBuffer failed");
			return false;
		}

		VkMemoryRequirements requirements{};
		vkGetBufferMemoryRequirements(logicalDevice, readbackBuffer, &requirements);

		VkMemoryAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocateInfo.allocationSize = requirements.size;
		allocateInfo.memoryTypeIndex = memoryTypeIndex;

		VkDeviceMemory readbackMemory = VK_NULL_HANDLE;
		if (vkAllocateMemory(logicalDevice, &allocateInfo, nullptr, &readbackMemory) != VK_SUCCESS)
		{
			vkDestroyBuffer(logicalDevice, readbackBuffer, nullptr);
			FG_CORE_ERROR("VulkanSwapchain::CaptureImage: vkAllocateMemory failed");
			return false;
		}

		vkBindBufferMemory(logicalDevice, readbackBuffer, readbackMemory, 0);

		// All rendering must be finished before the image can be copied.
		VulkanRendererAPI::WaitForGpuIdle();

		VkCommandBufferAllocateInfo commandInfo{};
		commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandInfo.commandPool = VulkanRendererAPI::GetCommandPool();
		commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		if (vkAllocateCommandBuffers(logicalDevice, &commandInfo, &commandBuffer) != VK_SUCCESS)
		{
			vkFreeMemory(logicalDevice, readbackMemory, nullptr);
			vkDestroyBuffer(logicalDevice, readbackBuffer, nullptr);
			FG_CORE_ERROR("VulkanSwapchain::CaptureImage: vkAllocateCommandBuffers failed");
			return false;
		}

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		// Swapchain images are in the LAYOUT_UNDEFINED / PRESENT_SRC layouts
		// between frames, so transition into TRANSFER_SRC_OPTIMAL before copying.
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;      // tightly packed
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { width, height, 1 };

		vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			readbackBuffer, 1, &region);

		vkEndCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		VkResult submitResult = vkQueueSubmit(VulkanRendererAPI::GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
		VkResult waitResult = VK_SUCCESS;
		if (submitResult == VK_SUCCESS)
			waitResult = vkQueueWaitIdle(VulkanRendererAPI::GetGraphicsQueue());

		bool ok = false;

		if (waitResult == VK_SUCCESS)
		{
			void* mapped = nullptr;
			if (vkMapMemory(logicalDevice, readbackMemory, 0, bufferSize, 0, &mapped) == VK_SUCCESS && mapped)
			{
				const uint8_t* src = static_cast<const uint8_t*>(mapped);

				// Write a binary PPM, converting BGRA to RGB. The top row of a
				// Vulkan image is the first row in memory.
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

				vkUnmapMemory(logicalDevice, readbackMemory);
			}
		}

		vkFreeCommandBuffers(logicalDevice, commandInfo.commandPool, 1, &commandBuffer);
		vkFreeMemory(logicalDevice, readbackMemory, nullptr);
		vkDestroyBuffer(logicalDevice, readbackBuffer, nullptr);

		if (!ok)
			FG_CORE_ERROR("VulkanSwapchain::CaptureImage: could not write '{0}'", path);
		else
			FG_CORE_INFO("VulkanSwapchain: captured {0}x{1} to '{2}'", width, height, path);

		return ok;
	}

}

#endif // FG_NO_VULKAN_RUNTIME
