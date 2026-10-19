#include "fgpch.h"
#include "Platform/Vulkan/Memory/VmaDeviceAllocator.h"

#include "Platform/Vulkan/Device/DebugName.h"
#include "Platform/Vulkan/Device/VkDevice.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

Ref<VmaDeviceAllocator> VmaDeviceAllocator::Create(Device& device)
{
	if (!device.CreateAllocator())
		return nullptr;
	return CreateRef<VmaDeviceAllocator>(device, device.GetAllocator(), false);
}

VmaDeviceAllocator::VmaDeviceAllocator(Device& device, VmaAllocator handle, bool ownsHandle)
	: m_Device(device), m_Handle(handle), m_OwnsHandle(ownsHandle)
{
}

VmaDeviceAllocator::~VmaDeviceAllocator()
{
	if (m_OwnsHandle && m_Handle != nullptr)
		vmaDestroyAllocator(m_Handle);
	m_Handle = nullptr;
}

VmaMemoryUsage VmaDeviceAllocator::ToUsage(MemoryPreference preference)
{
	switch (preference)
	{
	case MemoryPreference::PreferGpu:
		return VMA_MEMORY_USAGE_GPU_ONLY;
	case MemoryPreference::PreferCpu:
		return VMA_MEMORY_USAGE_CPU_ONLY;
	case MemoryPreference::None:
	default:
		return VMA_MEMORY_USAGE_UNKNOWN;
	}
}

bool VmaDeviceAllocator::MapMemory(const AllocationMapInfo& mapInfo, void*& outPointer)
{
	outPointer = nullptr;
	VmaAllocation allocation = static_cast<VmaAllocation>(mapInfo.Allocation.Handle);
	if (allocation == nullptr)
	{
		FG_CORE_ERROR("VMA: MapMemory on an allocation with no handle");
		return false;
	}

	std::lock_guard<std::mutex> lock(m_MapLock);
	const uint32_t count = m_MapCounts[mapInfo.Allocation.Handle];
	if (count > 0)
	{
		// A subset view shares its parent's map: raise the reference and
		// hand back the pointer VMA already has.
		m_MapCounts[mapInfo.Allocation.Handle] = count + 1;
		VmaAllocationInfo info{};
		vmaGetAllocationInfo(m_Handle, allocation, &info);
		outPointer = info.pMappedData;
		return outPointer != nullptr;
	}

	void* mapped = nullptr;
	const VkResult result = vmaMapMemory(m_Handle, allocation, &mapped);
	if (result != VK_SUCCESS)
	{
		FG_CORE_ERROR("VMA: vmaMapMemory failed ({0})", (int)result);
		return false;
	}

	m_MapCounts[mapInfo.Allocation.Handle] = 1;
	outPointer = mapped;
	return true;
}

void VmaDeviceAllocator::UnmapMemory(const AllocationMapInfo& mapInfo)
{
	VmaAllocation allocation = static_cast<VmaAllocation>(mapInfo.Allocation.Handle);
	if (allocation == nullptr)
		return;

	std::lock_guard<std::mutex> lock(m_MapLock);
	const auto entry = m_MapCounts.find(mapInfo.Allocation.Handle);
	if (entry == m_MapCounts.end())
	{
		FG_CORE_ERROR("VMA: UnmapMemory on an allocation that is not mapped");
		return;
	}

	if (--entry->second > 0)
		return;  // views remain; the map itself stays

	m_MapCounts.erase(entry);
	vmaUnmapMemory(m_Handle, allocation);
}

uint32_t VmaDeviceAllocator::GetMapCount(void* allocationHandle)
{
	std::lock_guard<std::mutex> lock(m_MapLock);
	const auto entry = m_MapCounts.find(allocationHandle);
	return entry == m_MapCounts.end() ? 0 : entry->second;
}

bool VmaDeviceAllocator::AllocateBuffer(
	const char* name,
	const VkBufferCreateInfo& bufferCreateInfo,
	const AllocationCreateInfo& allocationCreateInfo,
	BufferAllocationInfo& outAllocation)
{
	VmaAllocationCreateInfo vmaInfo{};
	vmaInfo.usage = ToUsage(allocationCreateInfo.Preference);
	vmaInfo.requiredFlags = allocationCreateInfo.RequiredProperties;
	vmaInfo.preferredFlags = allocationCreateInfo.PreferredProperties;
	// Device-address memory allocate flags are VMA's business: the allocator
	// was created with VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT and
	// VMA chains the flag itself; allocationCreateInfo.AllocateFlags is
	// carried on the request for allocators that need it explicitly.

	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = nullptr;
	VmaAllocationInfo info{};
	const VkResult result = vmaCreateBuffer(m_Handle, &bufferCreateInfo, &vmaInfo, &buffer, &allocation, &info);
	if (result != VK_SUCCESS)
	{
		FG_CORE_ERROR("VMA: vmaCreateBuffer '{0}' failed ({1})", name ? name : "(unnamed)", (int)result);
		return false;
	}

	outAllocation = BufferAllocationInfo{};
	outAllocation.Buffer = buffer;
	outAllocation.BufferOffset = 0;
	outAllocation.BufferSize = bufferCreateInfo.size;
	outAllocation.Usage = bufferCreateInfo.usage;
	outAllocation.Allocation.Allocator = this;
	outAllocation.Allocation.Memory = info.deviceMemory;
	outAllocation.Allocation.Size = info.size;
	outAllocation.Allocation.Offset = info.offset;
	outAllocation.Allocation.MemoryTypeIndex = static_cast<int>(info.memoryType);
	outAllocation.Allocation.Handle = allocation;

#if defined(FG_DEBUG)
	if (name && *name)
		SetDebugName(m_Device, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buffer), name);
#endif

	return true;
}

bool VmaDeviceAllocator::FreeAllocation(const BufferAllocationInfo& allocation)
{
	if (allocation.Allocation.Memory == VK_NULL_HANDLE)
		return false;  // nothing was allocated
	if (allocation.Allocation.Allocator != this)
	{
		FG_CORE_ERROR("VMA: buffer allocation was not made by this allocator; refusing to free it");
		return false;
	}
	if (GetMapCount(allocation.Allocation.Handle) > 0)
	{
		FG_CORE_ERROR("VMA: refusing to free a buffer that is still mapped");
		return false;
	}

	vmaDestroyBuffer(m_Handle, allocation.Buffer, static_cast<VmaAllocation>(allocation.Allocation.Handle));
	return true;
}

bool VmaDeviceAllocator::AllocateImage(
	const char* name,
	const VkImageCreateInfo& imageCreateInfo,
	const AllocationCreateInfo& allocationCreateInfo,
	ImageAllocationInfo& outAllocation)
{
	VmaAllocationCreateInfo vmaInfo{};
	vmaInfo.usage = ToUsage(allocationCreateInfo.Preference);
	vmaInfo.requiredFlags = allocationCreateInfo.RequiredProperties;
	vmaInfo.preferredFlags = allocationCreateInfo.PreferredProperties;

	VkImage image = VK_NULL_HANDLE;
	VmaAllocation allocation = nullptr;
	VmaAllocationInfo info{};
	const VkResult result = vmaCreateImage(m_Handle, &imageCreateInfo, &vmaInfo, &image, &allocation, &info);
	if (result != VK_SUCCESS)
	{
		FG_CORE_ERROR("VMA: vmaCreateImage '{0}' failed ({1})", name ? name : "(unnamed)", (int)result);
		return false;
	}

	outAllocation = ImageAllocationInfo{};
	outAllocation.Image = image;
	outAllocation.Extent = imageCreateInfo.extent;
	outAllocation.Format = imageCreateInfo.format;
	outAllocation.MipLevels = imageCreateInfo.mipLevels;
	outAllocation.ArrayLayers = imageCreateInfo.arrayLayers;
	outAllocation.Allocation.Allocator = this;
	outAllocation.Allocation.Memory = info.deviceMemory;
	outAllocation.Allocation.Size = info.size;
	outAllocation.Allocation.Offset = info.offset;
	outAllocation.Allocation.MemoryTypeIndex = static_cast<int>(info.memoryType);
	outAllocation.Allocation.Handle = allocation;

#if defined(FG_DEBUG)
	if (name && *name)
		SetDebugName(m_Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image), name);
#endif

	return true;
}

bool VmaDeviceAllocator::FreeAllocation(const ImageAllocationInfo& allocation)
{
	if (allocation.Allocation.Memory == VK_NULL_HANDLE)
		return false;
	if (allocation.Allocation.Allocator != this)
	{
		FG_CORE_ERROR("VMA: image allocation was not made by this allocator; refusing to free it");
		return false;
	}
	if (GetMapCount(allocation.Allocation.Handle) > 0)
	{
		FG_CORE_ERROR("VMA: refusing to free an image that is still mapped");
		return false;
	}

	vmaDestroyImage(m_Handle, allocation.Image, static_cast<VmaAllocation>(allocation.Allocation.Handle));
	return true;
}

}}
