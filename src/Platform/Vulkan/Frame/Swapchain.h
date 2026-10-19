#pragma once

#include "Platform/Vulkan/Frame/SwapchainParams.h"

#include <vulkan/vulkan.h>

#include <vector>

namespace Forge { namespace Vulkan {

class PhysicalDevice;
class Device;

// Owns the swapchain (BRUTAL VulkanBackend swapchain handling): format and
// present-mode selection with fallbacks, min image count from the present
// mode, PreTransform, the OldSwapchain recreation chain, OutOfDate ->
// rebuild-next-frame, Suboptimal -> flag, and the one-shot command buffer
// that initializes every backbuffer Undefined -> PresentSrcKHR so the
// first frame never renders to an image with an undefined layout.
class Swapchain final
{
public:
	struct CreateInfo
	{
		::VkSurfaceKHR Surface = VK_NULL_HANDLE;
		uint32_t Width = 0;
		uint32_t Height = 0;
		VkSurfaceFormatKHR PreferredFormat{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		VkPresentModeKHR PreferredPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
		::VkSwapchainKHR OldSwapchain = VK_NULL_HANDLE;  // recreation chain
		uint32_t GraphicsQueueFamily = 0;
		uint32_t PresentQueueFamily = 0;
	};

	Swapchain() = default;
	~Swapchain();

	Swapchain(const Swapchain&) = delete;
	Swapchain& operator=(const Swapchain&) = delete;

	[[nodiscard]] bool Create(const PhysicalDevice& physicalDevice, Device& device, const CreateInfo& createInfo);
	void Destroy();

	[[nodiscard]] ::VkSwapchainKHR Handle() const { return m_Handle; }
	[[nodiscard]] bool IsNull() const { return m_Handle == VK_NULL_HANDLE; }
	[[nodiscard]] const std::vector<::VkImage>& GetImages() const { return m_Images; }
	[[nodiscard]] VkExtent2D GetExtent() const { return m_Extent; }
	[[nodiscard]] VkSurfaceFormatKHR GetFormat() const { return m_Format; }
	[[nodiscard]] VkPresentModeKHR GetPresentMode() const { return m_PresentMode; }
	[[nodiscard]] VkImageAspectFlags GetDepthAspect() const;

	// Set when an acquire/present said OUT_OF_DATE: the caller rebuilds on
	// the next frame, passing this swapchain as OldSwapchain.
	[[nodiscard]] bool NeedsRebuild() const { return m_NeedRebuild; }
	[[nodiscard]] bool IsSuboptimal() const { return m_Suboptimal; }
	void ClearSuboptimal() { m_Suboptimal = false; }

	// Acquire hands out the image index and classifies the result;
	// RebuildNextFrame sets NeedsRebuild so the next Create rebuilds.
	[[nodiscard]] AcquireOutcome AcquireNextImage(uint64_t timeoutNanos, ::VkSemaphore semaphore, uint32_t& outImageIndex);

private:
	Device* m_Device = nullptr;
	::VkSwapchainKHR m_Handle = VK_NULL_HANDLE;
	// The backbuffers start undefined, and they stay that way until the
	// frame that acquires them: a presentable image may only be used after
	// vkAcquireNextImageKHR has returned it, so there is nothing to
	// transition before the first acquire. The present render pass
	// declares an UNDEFINED initial layout, so the transition into
	// COLOR_ATTACHMENT_OPTIMAL happens in the frame that owns the image,
	// and EndFrame takes it to PRESENT_SRC.
	std::vector<::VkImage> m_Images;
	VkExtent2D m_Extent{};
	VkSurfaceFormatKHR m_Format{};
	VkPresentModeKHR m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
	VkSurfaceTransformFlagBitsKHR m_PreTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	bool m_NeedRebuild = false;
	bool m_Suboptimal = false;
	VkResult m_LastAcquireFailure = VK_SUCCESS;  // acquire-failure log throttle
};

}}
