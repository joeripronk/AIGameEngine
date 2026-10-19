#pragma once

#include <vulkan/vulkan.h>

#include <mutex>
#include <span>

namespace Forge { namespace Vulkan {

class Device;

// A queue is the only door through which work reaches the GPU: every
// submit, present and wait-idle is serialized by this object's mutex, so
// command buffers may be recorded on any thread while submission goes
// through the queue object (BRUTAL Manual/Classes/Queue.cs).
class Queue final
{
public:
	Queue(const Device& device, uint32_t familyIndex, uint32_t queueIndex, ::VkQueue handle);

	Queue(const Queue&) = delete;
	Queue& operator=(const Queue&) = delete;

	[[nodiscard]] bool Submit(std::span<const ::VkSubmitInfo> submits, ::VkFence fence = VK_NULL_HANDLE);
	[[nodiscard]] bool Submit2(std::span<const ::VkSubmitInfo2> submits, ::VkFence fence = VK_NULL_HANDLE);

	// Present answers with the VkResult: SUCCESS / TIMEOUT / SUBOPTIMAL /
	// OUT_OF_DATE are decisions the frame loop takes, not failures to be
	// flattened into a bool.
	VkResult PresentKHR(const ::VkPresentInfoKHR& presentInfo);

	[[nodiscard]] bool WaitIdle();

	[[nodiscard]] ::VkQueue Handle() const { return m_Handle; }
	[[nodiscard]] uint32_t GetFamilyIndex() const { return m_FamilyIndex; }
	[[nodiscard]] uint32_t GetQueueIndex() const { return m_QueueIndex; }

private:
	const Device& m_Device;
	uint32_t m_FamilyIndex = 0;
	uint32_t m_QueueIndex = 0;
	::VkQueue m_Handle = VK_NULL_HANDLE;
	std::mutex m_SubmitLock;
};

}}
