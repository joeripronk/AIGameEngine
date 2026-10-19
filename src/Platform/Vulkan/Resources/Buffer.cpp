#include "fgpch.h"
#include "Platform/Vulkan/Resources/Buffer.h"

#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

Buffer::~Buffer()
{
	Dispose();
}

bool Buffer::Create(IBufferAllocator& allocator, const CreateInfo& createInfo)
{
	if (m_Info.Buffer != VK_NULL_HANDLE)
	{
		FG_CORE_WARN("Buffer::Create on an already-created buffer; Dispose() first");
		return false;
	}
	if (createInfo.Size == 0)
	{
		FG_CORE_ERROR("Buffer::Create with zero size ('{0}')", createInfo.Name);
		return false;
	}

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = createInfo.Size;
	bufferInfo.usage = createInfo.Usage;
	bufferInfo.sharingMode = createInfo.SharingMode;

	AllocationCreateInfo allocationInfo{};
	allocationInfo.Preference = createInfo.Preference;
	allocationInfo.RequiredProperties = createInfo.RequiredProperties;
	allocationInfo.PreferredProperties = createInfo.PreferredProperties;

	BufferAllocationInfo allocation{};
	if (!allocator.AllocateBuffer(createInfo.Name.c_str(), bufferInfo, allocationInfo, allocation))
		return false;

	m_Allocator = &allocator;
	m_Info = allocation;
	return true;
}

bool Buffer::Dispose()
{
	if (m_Info.Buffer == VK_NULL_HANDLE)
		return true;
	if (!m_Allocator)
		return false;

	if (!m_Allocator->FreeAllocation(m_Info))
		return false;

	m_Info = BufferAllocationInfo{};
	m_Allocator = nullptr;
	return true;
}

MappedMemory Buffer::Map() const
{
	if (m_Info.Buffer == VK_NULL_HANDLE)
		return MappedMemory{};

	AllocationMapInfo mapInfo{};
	mapInfo.Allocation = m_Info.Allocation;
	mapInfo.Region = MapRegion{ m_Info.BufferOffset, m_Info.BufferSize };
	return MappedMemory::Map(mapInfo);
}

}}
