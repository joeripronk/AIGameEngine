#include "fgpch.h"
#include "Platform/Vulkan/VkRendererInternal.h"

#include "Platform/Vulkan/Device/DebugName.h"
#include "Platform/Vulkan/Device/VkDevice.h"
#include "Platform/Vulkan/Memory/VmaDeviceAllocator.h"
#include "Platform/Vulkan/Resources/Subresources.h"
#include "Forge/Core/FileSystem.h"
#include "Forge/Core/Log.h"

#include <vk_mem_alloc.h>

#include <cstring>

namespace Forge { namespace Vulkan {

std::vector<uint32_t> ReadSpirv(const std::string& path)
{
	std::vector<uint8_t> bytes;
	if (!FileSystem::ReadFile(path, bytes))
		return {};
	if (bytes.size() % 4 != 0)
	{
		FG_CORE_ERROR("VkRenderer: '{0}' is not a SPIR-V word stream", path);
		return {};
	}
	std::vector<uint32_t> words(bytes.size() / 4);
	std::memcpy(words.data(), bytes.data(), bytes.size());
	return words;
}

::VkShaderModule LoadModule(const Device& device, const char* path)
{
	const std::vector<uint32_t> words = ReadSpirv(path);
	if (words.empty())
	{
		FG_CORE_ERROR("VkRenderer: shader SPIR-V missing '{0}'", path);
		return VK_NULL_HANDLE;
	}
	VkShaderModuleCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	info.codeSize = words.size() * sizeof(uint32_t);
	info.pCode = words.data();
	::VkShaderModule module = VK_NULL_HANDLE;
	if (device.GetFunctions().CreateShaderModule(device.Handle(), &info, nullptr, &module) != VK_SUCCESS)
	{
		FG_CORE_ERROR("VkRenderer: vkCreateShaderModule failed '{0}'", path);
		return VK_NULL_HANDLE;
	}
	return module;
}

::VkImage CreateImageImpl(VmaDeviceAllocator& allocator, VkImageType type, VkFormat format,
	VkExtent3D extent, uint32_t mipLevels, uint32_t arrayLayers, VkImageTiling tiling,
	VkImageUsageFlags usage, VkImageCreateFlags flags, const char* name, VmaAllocation& outAlloc)
{
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.flags = flags;
	imageInfo.imageType = type;
	imageInfo.format = format;
	imageInfo.extent = extent;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = arrayLayers;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = tiling;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	AllocationCreateInfo alloc{};
	alloc.Preference = MemoryPreference::PreferGpu;
	alloc.RequiredProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	ImageAllocationInfo allocation{};
	if (!allocator.AllocateImage(name, imageInfo, alloc, allocation))
		return VK_NULL_HANDLE;

	outAlloc = static_cast<VmaAllocation>(allocation.Allocation.Handle);
	return allocation.Image;
}

::VkImageView CreateViewImpl(const Device& device, ::VkImage image, VkFormat format,
	VkImageAspectFlags aspect, uint32_t layers, VkImageViewType type)
{
	VkImageViewCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	info.image = image;
	info.viewType = type;
	info.format = format;
	info.subresourceRange = Subresources::Full(aspect, 1, layers);
	::VkImageView view = VK_NULL_HANDLE;
	if (device.GetFunctions().CreateImageView(device.Handle(), &info, nullptr, &view) != VK_SUCCESS)
		FG_CORE_ERROR("VkRenderer: image view creation failed");
	return view;
}

::VkBuffer CreateBufferImpl(VmaDeviceAllocator& allocator, VkDeviceSize size,
	VkBufferUsageFlags usage, bool hostVisible, const char* name, VmaAllocation& outAlloc, void** outData)
{
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	AllocationCreateInfo alloc{};
	if (hostVisible)
	{
		alloc.Preference = MemoryPreference::PreferCpu;
		alloc.RequiredProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}
	else
	{
		alloc.Preference = MemoryPreference::PreferGpu;
		alloc.RequiredProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
	}

	BufferAllocationInfo allocation{};
	if (!allocator.AllocateBuffer(name, bufferInfo, alloc, allocation))
		return VK_NULL_HANDLE;

	outAlloc = static_cast<VmaAllocation>(allocation.Allocation.Handle);
	if (outData)
		*outData = nullptr;  // mapped by the caller through MappedMemory
	return allocation.Buffer;
}

::VkBuffer CreateHostBuffer(VmaDeviceAllocator& allocator, VkDeviceSize size, const char* name,
	VmaAllocation& outAlloc, void** outData)
{
	VkBufferCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	info.size = size;
	info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	AllocationCreateInfo alloc{};
	alloc.Preference = MemoryPreference::PreferCpu;
	alloc.RequiredProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

	BufferAllocationInfo allocation{};
	if (!allocator.AllocateBuffer(name, info, alloc, allocation))
		return VK_NULL_HANDLE;
	outAlloc = static_cast<VmaAllocation>(allocation.Allocation.Handle);

	// Host-coherent maps stay mapped for the buffer's life: the renderer
	// writes at record time, the fence ring serialises reuse.
	AllocationMapInfo mapInfo{};
	mapInfo.Allocation = allocation.Allocation;
	mapInfo.Region = MapRegion{ 0, WholeSize };
	void* data = nullptr;
	if (!allocator.MapMemory(mapInfo, data))
		return VK_NULL_HANDLE;
	*outData = data;
	return allocation.Buffer;
}

void DestroyHostBuffer(VmaDeviceAllocator& allocator, ::VkBuffer& buffer, VmaAllocation& alloc, void*& data)
{
	if (alloc != nullptr)
	{
		AllocationInfo info{};
		info.Handle = alloc;
		AllocationMapInfo mapInfo{};
		mapInfo.Allocation = info;
		allocator.UnmapMemory(mapInfo);
		alloc = nullptr;
	}
	data = nullptr;
	if (buffer != VK_NULL_HANDLE)
	{
		BufferAllocationInfo allocation{};
		allocation.Buffer = buffer;
		allocation.Allocation.Handle = nullptr;
		// The allocation was made by this allocator; rebuild its identity
		// from the handle pair. VMA destroys through the pair; a bare handle
		// pair is what the wrapper checks (allocator identity + map count).
		allocation.Allocation.Allocator = &allocator;
		if (!allocator.FreeAllocation(allocation))
			FG_CORE_WARN("VkRenderer: host buffer free refused");
		buffer = VK_NULL_HANDLE;
	}
}

::VkSampler CreateSamplerImpl(const Device& device, const VkSamplerCreateInfo& info, const char* name)
{
	::VkSampler sampler = VK_NULL_HANDLE;
	if (device.GetFunctions().CreateSampler(device.Handle(), &info, nullptr, &sampler) != VK_SUCCESS)
	{
		FG_CORE_ERROR("VkRenderer: sampler creation failed ('{0}')", name);
		return VK_NULL_HANDLE;
	}
#if defined(FG_DEBUG)
	SetDebugName(device, VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler), name);
#endif
	return sampler;
}

::VkRenderPass CreateRenderPassImpl(const Device& device,
	std::span<const VkAttachmentDescription> attachments,
	std::span<const VkAttachmentReference> colorRefs,
	const VkAttachmentReference* depthRef,
	VkFormat firstColorFormat)
{
	std::vector<VkAttachmentDescription> attachmentDesc(attachments.begin(), attachments.end());

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
	subpass.pColorAttachments = colorRefs.data();
	subpass.pDepthStencilAttachment = depthRef;

	std::vector<VkSubpassDependency> dependencies{
		VkSubpassDependency{
			VK_SUBPASS_EXTERNAL, 0,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_DEPENDENCY_BY_REGION_BIT },
		VkSubpassDependency{
			0, VK_SUBPASS_EXTERNAL,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT,
			VK_DEPENDENCY_BY_REGION_BIT },
	};

	VkRenderPassCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	info.attachmentCount = static_cast<uint32_t>(attachmentDesc.size());
	info.pAttachments = attachmentDesc.data();
	info.subpassCount = 1;
	info.pSubpasses = &subpass;
	info.dependencyCount = static_cast<uint32_t>(dependencies.size());
	info.pDependencies = dependencies.data();

	::VkRenderPass renderPass = VK_NULL_HANDLE;
	if (device.GetFunctions().CreateRenderPass(device.Handle(), &info, nullptr, &renderPass) != VK_SUCCESS)
		FG_CORE_ERROR("VkRenderer: render pass creation failed (first colour format {0})", (int)firstColorFormat);
	return renderPass;
}

::VkFramebuffer CreateFramebufferImpl(const Device& device, ::VkRenderPass renderPass,
	std::span<const ::VkImageView> views, uint32_t width, uint32_t height)
{
	std::vector<::VkImageView> attachments(views.begin(), views.end());
	VkFramebufferCreateInfo info{};
	info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	info.renderPass = renderPass;
	info.attachmentCount = static_cast<uint32_t>(attachments.size());
	info.pAttachments = attachments.data();
	info.width = width;
	info.height = height;
	info.layers = 1;
	::VkFramebuffer framebuffer = VK_NULL_HANDLE;
	if (device.GetFunctions().CreateFramebuffer(device.Handle(), &info, nullptr, &framebuffer) != VK_SUCCESS)
		FG_CORE_ERROR("VkRenderer: framebuffer creation failed");
	return framebuffer;
}

}}
