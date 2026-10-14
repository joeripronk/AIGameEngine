#pragma once

#include "Forge/Core/Base.h"

#include <vulkan/vulkan.h>
#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <vector>

namespace Forge {

	// Presentation for the Vulkan backend.
	//
	// nvrhi deliberately has no swapchain abstraction, so this owns the
	// VkSwapchainKHR, wraps each acquired VkImage as an nvrhi texture (via
	// nvrhi::ObjectTypes::VK_Image) and builds one nvrhi framebuffer per
	// swapchain image so the render passes can target them.
	class VulkanSwapchain
	{
	public:
		VulkanSwapchain() = default;
		~VulkanSwapchain();

		VulkanSwapchain(const VulkanSwapchain&) = delete;
		VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

		// Creates the swapchain for the given GLFW window. Returns false (with an
		// error logged) if presentation is unsupported for the surface.
		bool Create(void* windowHandle, uint32_t width, uint32_t height);

		// Recreates the swapchain, e.g. after a window resize or when the old
		// one went out of date. Existing framebuffers are discarded.
		bool Recreate(uint32_t width, uint32_t height);

		void Destroy();

		// Waits for the image to be ready and acquires the next one. Returns the
		// image index, or UINT32_MAX if the swapchain needs recreating or the
		// surface was lost.
		uint32_t AcquireNextImage();

		// Presents a previously acquired image. Returns false if the swapchain
		// should be recreated (out of date / suboptimal).
		bool Present(uint32_t imageIndex);

		nvrhi::FramebufferHandle GetFramebuffer(uint32_t imageIndex) const;
		nvrhi::Format GetFormat() const { return m_Format; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

		bool IsValid() const { return m_Swapchain != VK_NULL_HANDLE; }

	private:
		VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
		VkFormat m_VkFormat = VK_FORMAT_UNDEFINED;
		nvrhi::Format m_Format = nvrhi::Format::UNKNOWN;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		VkPresentModeKHR m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
		bool m_SurfaceOutOfDate = false;

		// nvrhi handles wrapping the swapchain images and their framebuffers.
		std::vector<nvrhi::TextureHandle> m_ImageTextures;
		std::vector<nvrhi::FramebufferHandle> m_Framebuffers;

		bool BuildImagesAndFramebuffers();
	};

}
