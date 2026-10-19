#pragma once

#include "Platform/Vulkan/Memory/AllocationInfo.h"

#include "Forge/Core/Base.h"

#include <vulkan/vulkan.h>

#include <span>
#include <type_traits>

namespace Forge { namespace Vulkan {

class Device;
class IBufferAllocator;
class IImageAllocator;

// Base of every allocator (BRUTAL Allocator/IAllocator.cs): the device it
// allocates on, and the map/unmap pair every mapped view runs through.
// Map/unmap are refcounted per allocation - a subset view shares its
// parent's map, and the memory stays mapped until the last view releases
// it, which is what makes "free refuses while mapped" enforceable.
class IAllocator
{
public:
	virtual ~IAllocator() = default;

	[[nodiscard]] virtual Device& GetDevice() = 0;

	[[nodiscard]] virtual bool MapMemory(const AllocationMapInfo& mapInfo, void*& outPointer) = 0;
	virtual void UnmapMemory(const AllocationMapInfo& mapInfo) = 0;

	[[nodiscard]] virtual uint32_t GetMapCount(void* allocationHandle) = 0;
};

struct BufferAllocationInfo
{
	::VkBuffer Buffer = VK_NULL_HANDLE;
	size_t BufferOffset = 0;
	size_t BufferSize = 0;
	VkBufferUsageFlags Usage = 0;
	AllocationInfo Allocation;
};

struct ImageAllocationInfo
{
	::VkImage Image = VK_NULL_HANDLE;
	::VkExtent3D Extent{};
	VkFormat Format = VK_FORMAT_UNDEFINED;
	uint32_t MipLevels = 1;
	uint32_t ArrayLayers = 1;
	AllocationInfo Allocation;
};

class IBufferAllocator : public virtual IAllocator
{
public:
	[[nodiscard]] virtual bool AllocateBuffer(
		const char* name,
		const VkBufferCreateInfo& bufferCreateInfo,
		const AllocationCreateInfo& allocationCreateInfo,
		BufferAllocationInfo& outAllocation) = 0;

	// False while the allocation is mapped, or when it was not made by this
	// allocator - releasing what is in use is the failure this reports.
	[[nodiscard]] virtual bool FreeAllocation(const BufferAllocationInfo& allocation) = 0;
};

class IImageAllocator : public virtual IAllocator
{
public:
	[[nodiscard]] virtual bool AllocateImage(
		const char* name,
		const VkImageCreateInfo& imageCreateInfo,
		const AllocationCreateInfo& allocationCreateInfo,
		ImageAllocationInfo& outAllocation) = 0;

	[[nodiscard]] virtual bool FreeAllocation(const ImageAllocationInfo& allocation) = 0;
};

// RAII mapped memory (BRUTAL MappedMemory + refcounted nested maps in
// DeviceEx): map through the owning allocator, read through a typed span,
// flush/invalidate the exact region, unmap on scope exit. Subset views
// share the parent map; releasing a view releases one reference, not the
// map itself.
class MappedMemory final
{
public:
	MappedMemory() = default;
	MappedMemory(MappedMemory&& other) noexcept;
	MappedMemory& operator=(MappedMemory&& other) noexcept;
	MappedMemory(const MappedMemory&) = delete;
	MappedMemory& operator=(const MappedMemory&) = delete;
	~MappedMemory();

	[[nodiscard]] static MappedMemory Map(const AllocationMapInfo& mapInfo);

	// A view into this mapped region. Invalid when the requested region
	// exceeds the parent (the check BRUTAL makes by throwing; here it
	// answers false and leaves the caller holding nothing new).
	[[nodiscard]] MappedMemory Offset(MapRegion region) const;
	[[nodiscard]] MappedMemory Offset(size_t bytes) const;

	[[nodiscard]] std::span<std::byte> AsSpan() const { return std::span<std::byte>(static_cast<std::byte*>(m_Pointer), m_Info.Region.Size); }

	template <typename T>
	[[nodiscard]] std::span<T> AsSpan() const
	{
		static_assert(std::is_trivially_copyable_v<T>, "mapped memory spans trivially copyable types only");
		if (!m_Pointer || m_Info.Region.Size < sizeof(T) ||
			(m_Info.Region.Size % sizeof(T)) != 0 ||
			(reinterpret_cast<uintptr_t>(m_Pointer) % alignof(T)) != 0)
			return {};
		return std::span<T>(static_cast<T*>(m_Pointer), m_Info.Region.Size / sizeof(T));
	}

	void Flush() const;
	void Invalidate() const;

	void Unmap();

	[[nodiscard]] void* Data() const { return m_Pointer; }
	[[nodiscard]] size_t Size() const { return m_Info.Region.Size; }
	[[nodiscard]] const MapRegion& GetRegion() const { return m_Info.Region; }
	[[nodiscard]] bool IsValid() const { return m_Pointer != nullptr; }

private:
	MappedMemory(const AllocationMapInfo& mapInfo, void* pointer);

	AllocationMapInfo m_Info{};
	void* m_Pointer = nullptr;
};

}}
