#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Forge { namespace Vulkan {

namespace Subresources {

	// Subresource range constructors (BRUTAL Extensions/SubresourceExtensions):
	// names that say what a range covers, so a view never quietly covers the
	// whole image when it meant one mip.

	constexpr VkImageSubresourceRange Full(VkImageAspectFlags aspect, uint32_t levelCount, uint32_t layerCount)
	{
		return VkImageSubresourceRange{ aspect, 0, levelCount, 0, layerCount };
	}

	constexpr VkImageSubresourceRange Single(VkImageAspectFlags aspect, uint32_t level, uint32_t layer)
	{
		return VkImageSubresourceRange{ aspect, level, 1, layer, 1 };
	}

	constexpr VkImageSubresourceRange Mips(VkImageAspectFlags aspect, uint32_t baseLevel, uint32_t levelCount, uint32_t layerCount, uint32_t baseLayer = 0)
	{
		return VkImageSubresourceRange{ aspect, baseLevel, levelCount, baseLayer, layerCount };
	}

	constexpr VkImageSubresourceRange Layers(VkImageAspectFlags aspect, uint32_t baseLayer, uint32_t layerCount, uint32_t levelCount = 1, uint32_t baseLevel = 0)
	{
		return VkImageSubresourceRange{ aspect, baseLevel, levelCount, baseLayer, layerCount };
	}

	constexpr VkImageSubresourceLayers FullLayers(VkImageAspectFlags aspect, uint32_t mipLevel, uint32_t baseLayer, uint32_t layerCount)
	{
		return VkImageSubresourceLayers{ aspect, mipLevel, baseLayer, layerCount };
	}

}

// The aspect(s) a format's data lives in. A view over the wrong aspect
// reads a plane the image does not have, so the mask is derived from the
// format and never guessed at the call site.
[[nodiscard]] constexpr VkImageAspectFlags FormatAspectMask(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_X8_D24_UNORM_PACK32:
	case VK_FORMAT_D32_SFLOAT:
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
		return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	case VK_FORMAT_S8_UINT:
		return VK_IMAGE_ASPECT_STENCIL_BIT;
	case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
		return VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT | VK_IMAGE_ASPECT_PLANE_2_BIT;
	default:
		return VK_IMAGE_ASPECT_COLOR_BIT;
	}
}

// Texel block size in bytes for the formats the engine imports; 0 for
// compressed formats the upload path does not handle.
[[nodiscard]] constexpr uint32_t FormatTexelBlockBytes(VkFormat format)
{
	switch (format)
	{
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8_UINT:
	case VK_FORMAT_R8_SINT:
	case VK_FORMAT_S8_UINT:
		return 1;
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R16_UNORM:
	case VK_FORMAT_R8G8_UINT:
		return 2;
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SRGB:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
	case VK_FORMAT_R32_SFLOAT:
	case VK_FORMAT_R32_UINT:
	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_D16_UNORM:
		return 4;
	case VK_FORMAT_R16G16_SFLOAT:
	case VK_FORMAT_R32G32_SFLOAT:
	case VK_FORMAT_R32G32_UINT:
	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
		return 8;
	case VK_FORMAT_R32G32B32_SFLOAT:
		return 12;
	case VK_FORMAT_R32G32B32A32_SFLOAT:
	case VK_FORMAT_R32G32B32A32_UINT:
		return 16;
	default:
		return 0;
	}
}

}}
