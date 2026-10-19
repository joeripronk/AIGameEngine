#include "fgpch.h"
#include "Platform/Vulkan/Resources/Image.h"

#include "Platform/Vulkan/Device/VkDevice.h"
#include "Platform/Vulkan/Resources/Subresources.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

Image::~Image()
{
	// Teardown: a mapped image cannot be released, and FreeAllocation logs
	// the refusal; the destructor has nowhere to report it.
	(void)Dispose();
}

bool Image::Create(IImageAllocator& allocator, const CreateInfo& createInfo)
{
	if (m_Info.Image != VK_NULL_HANDLE)
	{
		FG_CORE_WARN("Image::Create on an already-created image; Dispose() first");
		return false;
	}
	if (createInfo.Width == 0 || createInfo.Height == 0 || createInfo.Depth == 0)
	{
		FG_CORE_ERROR("Image::Create with a zero extent ('{0}')", createInfo.Name);
		return false;
	}

	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.flags = createInfo.Flags;
	imageInfo.imageType = createInfo.Type;
	imageInfo.format = createInfo.Format;
	imageInfo.extent = VkExtent3D{ createInfo.Width, createInfo.Height, createInfo.Depth };
	imageInfo.mipLevels = createInfo.MipLevels;
	imageInfo.arrayLayers = createInfo.ArrayLayers;
	imageInfo.samples = createInfo.Samples;
	imageInfo.tiling = createInfo.Tiling;
	imageInfo.usage = createInfo.Usage;
	imageInfo.sharingMode = createInfo.SharingMode;

	AllocationCreateInfo allocationInfo{};
	allocationInfo.Preference = createInfo.Preference;
	allocationInfo.RequiredProperties = createInfo.RequiredProperties;
	allocationInfo.PreferredProperties = createInfo.PreferredProperties;

	ImageAllocationInfo allocation{};
	if (!allocator.AllocateImage(createInfo.Name.c_str(), imageInfo, allocationInfo, allocation))
		return false;

	m_Allocator = &allocator;
	m_Info = allocation;
	m_FullRange = Subresources::Full(FormatAspectMask(createInfo.Format), createInfo.MipLevels, createInfo.ArrayLayers);
	return true;
}

bool Image::Dispose()
{
	if (m_Info.Image == VK_NULL_HANDLE)
		return true;
	if (!m_Allocator)
		return false;

	if (!m_Allocator->FreeAllocation(m_Info))
		return false;

	m_Info = ImageAllocationInfo{};
	m_Allocator = nullptr;
	m_FullRange = VkImageSubresourceRange{};
	return true;
}

bool Image::CreateView(const Device& device, ::VkImageView& outView, VkImageAspectFlags aspectOverride) const
{
	VkImageSubresourceRange range = m_FullRange;
	if (aspectOverride != 0)
		range.aspectMask = aspectOverride;
	return CreateView(device, outView, range);
}

bool Image::CreateView(const Device& device, ::VkImageView& outView, const VkImageSubresourceRange& range) const
{
	outView = VK_NULL_HANDLE;
	if (m_Info.Image == VK_NULL_HANDLE)
	{
		FG_CORE_ERROR("Image::CreateView on an image that does not exist");
		return false;
	}
	if (range.aspectMask == 0 || range.levelCount == 0 || range.layerCount == 0)
	{
		FG_CORE_ERROR("Image::CreateView with an empty subresource range");
		return false;
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = m_Info.Image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = m_Info.Format;
	viewInfo.subresourceRange = range;

	const VkResult result = device.GetFunctions().CreateImageView(device.Handle(), &viewInfo, nullptr, &outView);
	if (result != VK_SUCCESS)
	{
		FG_CORE_ERROR("Image::CreateView failed ({0})", (int)result);
		outView = VK_NULL_HANDLE;
		return false;
	}
	return true;
}

}}
