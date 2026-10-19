#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>

namespace Forge { namespace Vulkan {

class IAllocator;

// Where an allocation should live, mapped by the allocator onto its own
// usage vocabulary (BRUTAL AllocationInfo / MemoryPreference).
enum class MemoryPreference : uint8_t
{
	None,
	PreferGpu,
	PreferCpu,
};

// VMA-shaped description of a finished allocation: which allocator made it,
// where it landed, and the allocator-private handle. The allocator pointer
// is what makes FreeAllocation land on the allocator that made the
// allocation - a free handed to a different allocator is a bug, and saying
// so beats corrupting both.
struct AllocationInfo
{
	IAllocator* Allocator = nullptr;
	::VkDeviceMemory Memory = VK_NULL_HANDLE;
	size_t Size = 0;
	size_t Offset = 0;
	int MemoryTypeIndex = -1;
	void* Handle = nullptr;
};

// Request side of an allocation.
struct AllocationCreateInfo
{
	MemoryPreference Preference = MemoryPreference::None;
	VkMemoryPropertyFlags RequiredProperties = 0;
	VkMemoryPropertyFlags PreferredProperties = 0;
	float Priority = 0.5f;
	VkMemoryAllocateFlags AllocateFlags = 0;   // e.g. VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT
	const void* Next = nullptr;
};

// Byte region of a mapped allocation. WholeSize means "to the end".
constexpr size_t WholeSize = VK_WHOLE_SIZE;

struct MapRegion
{
	size_t Offset = 0;
	size_t Size = WholeSize;
};

struct AllocationMapInfo
{
	AllocationInfo Allocation;
	MapRegion Region;
};

// Pure subset rule: resolves a requested region against the parent region
// and reports whether it fits, with WHOLE_SIZE resolved against what is
// left. The validation a subset view must perform, testable with no device.
[[nodiscard]] bool ValidateMapSubset(MapRegion parent, MapRegion requested, MapRegion& outResolved);

}}
