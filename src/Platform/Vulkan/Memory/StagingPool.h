#pragma once

#include "Forge/Core/Base.h"
#include "Platform/Vulkan/Memory/Allocator.h"
#include "Platform/Vulkan/Resources/Buffer.h"

#include <vulkan/vulkan.h>

#include <stack>
#include <unordered_map>
#include <vector>

namespace Forge { namespace Vulkan {

class Device;
class Queue;

// The record/submit/reclaim/rewind bookkeeping of a staging pool, as plain
// counters: which command buffers are recorded, which are submitted, which
// staged buffers belong to submitted work and may be recycled, and when the
// pool may be rewound (only once nothing is left recorded). This is the
// state machine the pool runs - and what the unit tests drive with no
// device attached.
struct StagingPoolState final
{
	uint32_t CommandBuffersRecorded = 0;   // cursor: recorded, maybe unsubmitted
	uint32_t Submitted = 0;               // command buffers handed to the queue
	uint32_t StagedBuffers = 0;           // buffers in caller hands or submitted
	uint32_t SubmittedStagedBuffers = 0;  // of those, owned by submitted work
	uint32_t PendingFences = 0;

	[[nodiscard]] uint32_t PendingCommandBuffers() const { return CommandBuffersRecorded - Submitted; }

	// One batch submit: one fence, everything recorded so far.
	[[nodiscard]] bool Submit()
	{
		const uint32_t pending = PendingCommandBuffers();
		if (pending == 0)
			return false;
		++PendingFences;
		Submitted += pending;
		SubmittedStagedBuffers = StagedBuffers;
		return true;
	}

	// Fences signalled: reclaim the fences and the staged buffers behind
	// submitted work. Safe while later command buffers are still recorded
	// but unsubmitted.
	[[nodiscard]] bool Reclaim()
	{
		if (PendingFences == 0)
			return false;
		PendingFences = 0;
		StagedBuffers -= SubmittedStagedBuffers;
		SubmittedStagedBuffers = 0;
		return true;
	}

	// The pool resets as a whole, so only rewind once nothing is left
	// recorded.
	[[nodiscard]] bool CanRewind() const { return Submitted == CommandBuffersRecorded; }

	void Rewind()
	{
		CommandBuffersRecorded = 0;
		Submitted = 0;
	}
};

// Host-coherent staging buffers pooled by size, multi-record command buffers
// with a cursor, batch submits under one recycled fence (BRUTAL
// StagingPool.cs). Record on any thread, submit through the pool.
class StagingPool final
{
public:
	StagingPool(IBufferAllocator& allocator, Ref<Queue> queue, uint32_t initialCommandBufferCount);
	~StagingPool();

	StagingPool(const StagingPool&) = delete;
	StagingPool& operator=(const StagingPool&) = delete;

	// The next command buffer at the cursor; grows the list when the cursor
	// catches up with the allocated count. VK_NULL_HANDLE on failure - the
	// caller records nothing rather than recording into nothing.
	[[nodiscard]] ::VkCommandBuffer NextCommandBuffer();

	// A host-coherent TRANSFER_SRC buffer of the requested size, recycled
	// from the size bucket when one matches.
	[[nodiscard]] Ref<Buffer> AddStagingBuffer(size_t bytes);
	[[nodiscard]] Ref<Buffer> AddStagingBuffer(const void* data, size_t bytes);

	// Submit every recorded-but-unsubmitted command buffer under one fence.
	[[nodiscard]] bool Submit(std::span<const ::VkSemaphore> waitSemaphores = {},
		std::span<const VkPipelineStageFlags> waitDstStageMask = {},
		std::span<const ::VkSemaphore> signalSemaphores = {});

	// True when all outstanding fences are signalled; reclaims them and the
	// staged buffers behind submitted work, and rewinds when nothing is
	// left recorded.
	[[nodiscard]] bool WaitFor(int64_t timeoutNanos);
	[[nodiscard]] bool Wait();

	const StagingPoolState& GetState() const { return m_State; }

	void Dispose();

private:
	IBufferAllocator& m_Allocator;
	Device& m_Device;
	Ref<Queue> m_Queue;

	::VkCommandPool m_CommandPool = VK_NULL_HANDLE;
	std::vector<::VkCommandBuffer> m_CommandBuffers;

	std::vector<Ref<Buffer>> m_StagingBuffers;
	std::unordered_map<size_t, std::stack<Ref<Buffer>>> m_FreeStagingBuffers;

	std::vector<::VkFence> m_Fences;
	std::stack<::VkFence> m_FreeFences;

	StagingPoolState m_State{};
	bool m_Disposed = false;
};

}}
