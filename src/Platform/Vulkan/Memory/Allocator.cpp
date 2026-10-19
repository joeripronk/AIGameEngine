#include "fgpch.h"
#include "Platform/Vulkan/Memory/Allocator.h"

#include "Platform/Vulkan/Device/VkDevice.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

MappedMemory::MappedMemory(const AllocationMapInfo& mapInfo, void* pointer)
	: m_Info(mapInfo), m_Pointer(pointer)
{
}

MappedMemory::MappedMemory(MappedMemory&& other) noexcept
	: m_Info(other.m_Info), m_Pointer(other.m_Pointer)
{
	other.m_Pointer = nullptr;
}

MappedMemory& MappedMemory::operator=(MappedMemory&& other) noexcept
{
	if (this != &other)
	{
		Unmap();
		m_Info = other.m_Info;
		m_Pointer = other.m_Pointer;
		other.m_Pointer = nullptr;
	}
	return *this;
}

MappedMemory::~MappedMemory()
{
	Unmap();
}

MappedMemory MappedMemory::Map(const AllocationMapInfo& mapInfo)
{
	IAllocator* allocator = mapInfo.Allocation.Allocator;
	if (!allocator)
	{
		FG_CORE_ERROR("MappedMemory::Map on an allocation with no allocator");
		return MappedMemory{};
	}

	void* pointer = nullptr;
	if (!allocator->MapMemory(mapInfo, pointer))
		return MappedMemory{};

	return MappedMemory(mapInfo, pointer);
}

MappedMemory MappedMemory::Offset(MapRegion region) const
{
	if (!m_Pointer)
		return MappedMemory{};

	MapRegion resolved{};
	if (!ValidateMapSubset(m_Info.Region, region, resolved))
	{
		FG_CORE_ERROR("MappedMemory::Offset region {0}+{1} exceeds mapped region of {2}",
			region.Offset, region.Size, m_Info.Region.Size);
		return MappedMemory{};
	}

	AllocationMapInfo viewInfo = m_Info;
	viewInfo.Region = resolved;

	// The view shares the parent map: MapMemory is refcounted, so this
	// raises the count by one and hands back the existing pointer.
	void* pointer = nullptr;
	if (!m_Info.Allocation.Allocator->MapMemory(viewInfo, pointer))
		return MappedMemory{};

	// MapMemory reports the pointer at the allocation base; the view's own
	// pointer is the region offset into the map.
	return MappedMemory(viewInfo, static_cast<char*>(m_Pointer) + (resolved.Offset - m_Info.Region.Offset));
}

MappedMemory MappedMemory::Offset(size_t bytes) const
{
	return Offset(MapRegion{ bytes, WholeSize });
}

void MappedMemory::Flush() const
{
	if (!m_Pointer)
		return;

	VkMappedMemoryRange range{};
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.memory = m_Info.Allocation.Memory;
	range.offset = m_Info.Region.Offset;
	range.size = m_Info.Region.Size;

	const VkResult result = m_Info.Allocation.Allocator->GetDevice().GetFunctions().FlushMappedMemoryRanges(
		m_Info.Allocation.Allocator->GetDevice().Handle(), 1, &range);
	if (result != VK_SUCCESS)
		FG_CORE_ERROR("MappedMemory::Flush failed ({0})", (int)result);
}

void MappedMemory::Invalidate() const
{
	if (!m_Pointer)
		return;

	VkMappedMemoryRange range{};
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.memory = m_Info.Allocation.Memory;
	range.offset = m_Info.Region.Offset;
	range.size = m_Info.Region.Size;

	const VkResult result = m_Info.Allocation.Allocator->GetDevice().GetFunctions().InvalidateMappedMemoryRanges(
		m_Info.Allocation.Allocator->GetDevice().Handle(), 1, &range);
	if (result != VK_SUCCESS)
		FG_CORE_ERROR("MappedMemory::Invalidate failed ({0})", (int)result);
}

void MappedMemory::Unmap()
{
	if (!m_Pointer)
		return;

	m_Info.Allocation.Allocator->UnmapMemory(m_Info);
	m_Pointer = nullptr;
}

}}
