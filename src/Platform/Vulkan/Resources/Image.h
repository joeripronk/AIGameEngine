#pragma once

#include "Platform/Vulkan/Memory/Allocator.h"

#include <vulkan/vulkan.h>

#include <string>

namespace Forge { namespace Vulkan {

class Device;

// RAII image carrying its allocator (BRUTAL ImageEx): the full subresource
// range and mip count are facts of the creation, cached so every view and
// every barrier asks the same question.
class Image final
{
public:
	struct CreateInfo
	{
		std::string Name = "Image";
		VkFormat Format = VK_FORMAT_R8G8B8A8_UNORM;
		uint32_t Width = 1;
		uint32_t Height = 1;
		uint32_t Depth = 1;
		uint32_t MipLevels = 1;
		uint32_t ArrayLayers = 1;
		VkImageType Type = VK_IMAGE_TYPE_2D;
		VkImageTiling Tiling = VK_IMAGE_TILING_OPTIMAL;
		VkImageUsageFlags Usage = 0;
		VkSharingMode SharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VkSampleCountFlagBits Samples = VK_SAMPLE_COUNT_1_BIT;
		VkImageCreateFlags Flags = 0;
		MemoryPreference Preference = MemoryPreference::PreferGpu;
		VkMemoryPropertyFlags RequiredProperties = 0;
		VkMemoryPropertyFlags PreferredProperties = 0;
	};

	Image() = default;
	~Image();

	Image(const Image&) = delete;
	Image& operator=(const Image&) = delete;

	[[nodiscard]] bool Create(IImageAllocator& allocator, const CreateInfo& createInfo);

	[[nodiscard]] bool Dispose();

	// A view over this image. The default covers every mip and layer with
	// the format's own aspect mask; a caller asking for one mip says so
	// with the range it passes.
	[[nodiscard]] bool CreateView(const Device& device, ::VkImageView& outView, VkImageAspectFlags aspectOverride = 0) const;
	[[nodiscard]] bool CreateView(const Device& device, ::VkImageView& outView, const VkImageSubresourceRange& range) const;

	[[nodiscard]] ::VkImage Handle() const { return m_Info.Image; }
	[[nodiscard]] VkFormat Format() const { return m_Info.Format; }
	[[nodiscard]] uint32_t MaxMipLevels() const { return m_Info.MipLevels; }
	[[nodiscard]] uint32_t ArrayLayers() const { return m_Info.ArrayLayers; }
	[[nodiscard]] const VkImageSubresourceRange& GetFullRange() const { return m_FullRange; }
	[[nodiscard]] const ImageAllocationInfo& GetInfo() const { return m_Info; }
	[[nodiscard]] bool IsNull() const { return m_Info.Image == VK_NULL_HANDLE; }

private:
	IImageAllocator* m_Allocator = nullptr;
	ImageAllocationInfo m_Info{};
	VkImageSubresourceRange m_FullRange{};
};

}}
