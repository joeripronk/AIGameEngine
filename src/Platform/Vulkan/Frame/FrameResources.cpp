#include "fgpch.h"
#include "Platform/Vulkan/Frame/FrameResources.h"

#include "Platform/Vulkan/Device/VkDevice.h"

namespace Forge { namespace Vulkan {

void DestroyFrameResources(Device& device, FrameResources& resources)
{
	const auto functions = device.GetFunctions();

	if (resources.ColorImageView != VK_NULL_HANDLE)
		functions.DestroyImageView(device.Handle(), resources.ColorImageView, nullptr);
	resources.ColorImageView = VK_NULL_HANDLE;

	if (resources.DepthImageView != VK_NULL_HANDLE)
		functions.DestroyImageView(device.Handle(), resources.DepthImageView, nullptr);
	resources.DepthImageView = VK_NULL_HANDLE;

	if (resources.MultisampledImageView != VK_NULL_HANDLE)
		functions.DestroyImageView(device.Handle(), resources.MultisampledImageView, nullptr);
	resources.MultisampledImageView = VK_NULL_HANDLE;

	// The swapchain owns its backbuffer: an offscreen color image (the one
	// case where OwnsColorImage is true) is the only image destroyed here.
	if (resources.OwnsColorImage && resources.ColorImage != VK_NULL_HANDLE)
		functions.DestroyImage(device.Handle(), resources.ColorImage, nullptr);
	resources.ColorImage = VK_NULL_HANDLE;
	resources.OwnsColorImage = false;

	if (resources.DepthImage != VK_NULL_HANDLE)
		functions.DestroyImage(device.Handle(), resources.DepthImage, nullptr);
	resources.DepthImage = VK_NULL_HANDLE;

	if (resources.MultisampledImage != VK_NULL_HANDLE)
		functions.DestroyImage(device.Handle(), resources.MultisampledImage, nullptr);
	resources.MultisampledImage = VK_NULL_HANDLE;

	if (resources.Framebuffer != VK_NULL_HANDLE)
		functions.DestroyFramebuffer(device.Handle(), resources.Framebuffer, nullptr);
	resources.Framebuffer = VK_NULL_HANDLE;
}

}}
