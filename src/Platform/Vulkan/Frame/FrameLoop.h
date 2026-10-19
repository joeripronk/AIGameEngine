#pragma once

#include "Platform/Vulkan/Frame/Swapchain.h"
#include "Platform/Vulkan/Frame/SyncObjects.h"

#include "Forge/Core/Base.h"

#include <vector>

namespace Forge { namespace Vulkan {

class Device;
class Queue;

// The reference frame loop (BRUTAL VulkanBackend:1205-1353): acquire ->
// fence wait -> ResetCommandPool -> Begin(OneTimeSubmit) -> record -> End ->
// submit -> present. The acquire-semaphore ring has ImageCount + 1 slots
// and rotates per submitted frame, independently of the frame index, so
// two frames in flight can never share a semaphore; per-image fences and
// one-shot command buffers live in the SyncObjects chain.
//
// OUT_OF_DATE from acquire or present sets NeedsRebuild; the caller
// rebuilds the swapchain on the next BeginFrame, passing this one as
// OldSwapchain - the recreation chain never destroys a swapchain that
// frames are still referencing.
class FrameLoop final
{
public:
	[[nodiscard]] static Ref<FrameLoop> Create(Device& device, Ref<Queue> queue, Swapchain& swapchain, uint32_t commandBufferCount = 1);
	~FrameLoop();

	FrameLoop(const FrameLoop&) = delete;
	FrameLoop& operator=(const FrameLoop&) = delete;

	// Public construction: the factory above fills the object; CreateRef
	// needs the constructor reachable.
	FrameLoop() = default;

	// Acquires the next image and prepares its command buffer (fence wait,
	// pool reset, begin). Outcomes other than Ok/Suboptimal mean "no frame
	// this time": RebuildNextFrame flags the swapchain, and the caller
	// skips rendering without dropping the frame index.
	[[nodiscard]] AcquireOutcome BeginFrame(uint32_t& outImageIndex, SyncObjects& outSync);

	// End the command buffer, submit under the per-image fence (wait on the
	// acquire semaphore, signal the render-finished semaphore), present on
	// the queue, rotate the acquire ring.
	[[nodiscard]] PresentOutcome EndFrame(SyncObjects& sync);

	[[nodiscard]] uint32_t GetImageCount() const { return static_cast<uint32_t>(m_Swapchain->GetImages().size()); }
	[[nodiscard]] const SemaphoreRing& GetAcquireRing() const { return m_AcquireRing; }

	void Destroy();

private:
	Device* m_Device = nullptr;
	Ref<Queue> m_Queue;
	Swapchain* m_Swapchain = nullptr;

	::VkCommandPool m_CommandPool = VK_NULL_HANDLE;
	std::vector<SyncObjects> m_Frames;

	// ImageCount + 1 acquire semaphores, rotated independently of the frame
	// index (BRUTAL VulkanBackend).
	std::vector<::VkSemaphore> m_AcquireSemaphores;
	SemaphoreRing m_AcquireRing{};

	bool m_Destroyed = false;
};

}}
