#pragma once

#include "Platform/Vulkan/Memory/Allocator.h"

#include <vulkan/vulkan.h>

#include <string>

namespace Forge { namespace Vulkan {

// RAII buffer carrying the allocator that made it (BRUTAL BufferEx):
// Dispose returns the buffer to whichever allocator created it, and Map
// hands back RAII mapped memory that releases its reference on scope exit.
class Buffer final
{
public:
	struct CreateInfo
	{
		std::string Name = "Buffer";
		size_t Size = 0;
		VkBufferUsageFlags Usage = 0;
		MemoryPreference Preference = MemoryPreference::None;
		VkMemoryPropertyFlags RequiredProperties = 0;
		VkMemoryPropertyFlags PreferredProperties = 0;
		VkSharingMode SharingMode = VK_SHARING_MODE_EXCLUSIVE;
	};

	Buffer() = default;
	~Buffer();

	Buffer(const Buffer&) = delete;
	Buffer& operator=(const Buffer&) = delete;

	[[nodiscard]] bool Create(IBufferAllocator& allocator, const CreateInfo& createInfo);

	// False when the buffer is mapped (the mapped views own it) or was
	// already disposed.
	bool Dispose();

	[[nodiscard]] MappedMemory Map() const;

	[[nodiscard]] ::VkBuffer Handle() const { return m_Info.Buffer; }
	[[nodiscard]] size_t Size() const { return m_Info.BufferSize; }
	[[nodiscard]] const BufferAllocationInfo& GetInfo() const { return m_Info; }
	[[nodiscard]] IBufferAllocator* GetAllocator() const { return m_Allocator; }
	[[nodiscard]] bool IsNull() const { return m_Info.Buffer == VK_NULL_HANDLE; }

private:
	IBufferAllocator* m_Allocator = nullptr;
	BufferAllocationInfo m_Info{};
};

}}
