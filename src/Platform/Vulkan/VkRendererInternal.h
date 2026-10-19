#pragma once

// Shared creation helpers for the VkRenderer translation units
// (VkRendererDevice.cpp / VkRendererResources.cpp / VkRendererScene.cpp).
// These are renderer-internal builders over the Phase 1-4 layers, not
// engine API.

#include "Platform/Vulkan/VkRenderer.h"

#include <span>
#include <string>
#include <vector>

namespace Forge { namespace Vulkan {

// Reads resources/shaders/*.spv into a SPIR-V word stream. Empty on any
// failure - an empty stream is reported by the caller, not logged here.
[[nodiscard]] std::vector<uint32_t> ReadSpirv(const std::string& path);

// VkShaderModule from a .spv file on disk. VK_NULL_HANDLE (logged) when the
// file is missing or the driver rejects the module.
[[nodiscard]] ::VkShaderModule LoadModule(const Device& device, const char* path);

[[nodiscard]] ::VkImage CreateImageImpl(VmaDeviceAllocator& allocator, VkImageType type, VkFormat format,
	VkExtent3D extent, uint32_t mipLevels, uint32_t arrayLayers, VkImageTiling tiling,
	VkImageUsageFlags usage, VkImageCreateFlags flags, const char* name, VmaAllocation& outAlloc);

[[nodiscard]] ::VkImageView CreateViewImpl(const Device& device, ::VkImage image, VkFormat format,
	VkImageAspectFlags aspect, uint32_t layers, VkImageViewType type);

[[nodiscard]] ::VkBuffer CreateBufferImpl(VmaDeviceAllocator& allocator, VkDeviceSize size,
	VkBufferUsageFlags usage, bool hostVisible, const char* name, VmaAllocation& outAlloc, void** outData);

// Host-visible, host-coherent, mapped-for-life uniform buffer.
[[nodiscard]] ::VkBuffer CreateHostBuffer(VmaDeviceAllocator& allocator, VkDeviceSize size, const char* name,
	VmaAllocation& outAlloc, void** outData);

void DestroyHostBuffer(VmaDeviceAllocator& allocator, ::VkBuffer& buffer, VmaAllocation& alloc, void*& data);

[[nodiscard]] ::VkSampler CreateSamplerImpl(const Device& device, const VkSamplerCreateInfo& info, const char* name);

[[nodiscard]] ::VkRenderPass CreateRenderPassImpl(const Device& device,
	std::span<const VkAttachmentDescription> attachments,
	std::span<const VkAttachmentReference> colorRefs,
	const VkAttachmentReference* depthRef,
	VkFormat firstColorFormat);

[[nodiscard]] ::VkFramebuffer CreateFramebufferImpl(const Device& device, ::VkRenderPass renderPass,
	std::span<const ::VkImageView> views, uint32_t width, uint32_t height);

}}
