#pragma once

#include "Forge/Core/Base.h"
#include "Platform/Vulkan/Memory/Allocator.h"

#include <vk_mem_alloc.h>

#include <mutex>
#include <unordered_map>

namespace Forge { namespace Vulkan {

class Device;

// VMA is the only allocator implementation for now; the interfaces above
// exist so a ring/buddy allocator can be added later without touching a
// call site (BRUTAL Brutal.Vulkan.Vma/VmaAllocator.cs). Preference maps to
// VmaMemoryUsage; the map refcount is ours, VMA's is not - a subset view
// shares its parent's map, and the allocation refuses to free while any
// view still holds one.
class VmaDeviceAllocator final : public IBufferAllocator, public IImageAllocator
{
public:
	// Creates its own VmaAllocator over the device (the device's
	// CreateAllocator must have run).
	[[nodiscard]] static Ref<VmaDeviceAllocator> Create(Device& device);

	// Adopts an existing VmaAllocator; the allocator object is not destroyed
	// by this wrapper.
	VmaDeviceAllocator(Device& device, VmaAllocator handle, bool ownsHandle);
	~VmaDeviceAllocator() override;

	VmaDeviceAllocator(const VmaDeviceAllocator&) = delete;
	VmaDeviceAllocator& operator=(const VmaDeviceAllocator&) = delete;

	[[nodiscard]] Device& GetDevice() override { return m_Device; }
	[[nodiscard]] VmaAllocator Handle() const { return m_Handle; }

	[[nodiscard]] bool MapMemory(const AllocationMapInfo& mapInfo, void*& outPointer) override;
	void UnmapMemory(const AllocationMapInfo& mapInfo) override;
	[[nodiscard]] uint32_t GetMapCount(void* allocationHandle) override;

	[[nodiscard]] bool AllocateBuffer(
		const char* name,
		const VkBufferCreateInfo& bufferCreateInfo,
		const AllocationCreateInfo& allocationCreateInfo,
		BufferAllocationInfo& outAllocation) override;
	[[nodiscard]] bool FreeAllocation(const BufferAllocationInfo& allocation) override;

	[[nodiscard]] bool AllocateImage(
		const char* name,
		const VkImageCreateInfo& imageCreateInfo,
		const AllocationCreateInfo& allocationCreateInfo,
		ImageAllocationInfo& outAllocation) override;
	[[nodiscard]] bool FreeAllocation(const ImageAllocationInfo& allocation) override;

private:
	static VmaMemoryUsage ToUsage(MemoryPreference preference);
	void ChainAllocateFlags(AllocationInfo& info);

	Device& m_Device;
	VmaAllocator m_Handle = nullptr;
	bool m_OwnsHandle = false;

	std::mutex m_MapLock;
	std::unordered_map<void*, uint32_t> m_MapCounts;
};

}}
