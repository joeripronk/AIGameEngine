#pragma once

#include <vulkan/vulkan.h>

namespace Forge { namespace Vulkan {

class Device;
// Framebuffer and attachments for one swapchain image (BRUTAL
// FrameResources.cs). OwnsColorImage is the flag that keeps a swapchain
// image alive: the backbuffer belongs to the swapchain, and disposing frame
// resources must never destroy it.
struct FrameResources final
{
	uint32_t FrameIndex = 0;

	::VkFramebuffer Framebuffer = VK_NULL_HANDLE;

	::VkImage ColorImage = VK_NULL_HANDLE;
	::VkImageView ColorImageView = VK_NULL_HANDLE;
	bool OwnsColorImage = false;

	::VkImage DepthImage = VK_NULL_HANDLE;
	::VkImageView DepthImageView = VK_NULL_HANDLE;

	::VkImage MultisampledImage = VK_NULL_HANDLE;
	::VkImageView MultisampledImageView = VK_NULL_HANDLE;
};

// Destroys the views, the owned color image, the depth/multisample images
// and the framebuffer. Every handle is nulled so a disposed struct cannot
// be disposed twice.
void DestroyFrameResources(Device& device, FrameResources& resources);

}}
