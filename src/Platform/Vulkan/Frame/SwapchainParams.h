#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace Forge { namespace Vulkan {

// The swapchain questions answered as pure functions (BRUTAL VulkanBackend
// helpers): format, present mode, image count, and the classification of
// acquire/present results into frame-loop decisions. The device code asks
// these questions of real capability data; the tests ask them of tables.

// The exact preferred format if offered, otherwise the first candidate -
// the classic rule, which keeps a device that cannot do the preferred
// format working instead of failing.
[[nodiscard]] VkSurfaceFormatKHR SelectSurfaceFormat(std::span<const VkSurfaceFormatKHR> candidates, VkSurfaceFormatKHR preferred);

// The preferred present mode if offered, else FIFO (the mode every
// implementation must support).
[[nodiscard]] VkPresentModeKHR SelectPresentMode(std::span<const VkPresentModeKHR> modes, VkPresentModeKHR preferred);

// Minimum image count: mailbox wants at least 3 to queue frames ahead,
// everything else takes the surface's minimum; clamped to maxImageCount
// when the surface caps it (0 = uncapped).
[[nodiscard]] uint32_t ComputeMinImageCount(VkPresentModeKHR presentMode, uint32_t minImageCount, uint32_t maxImageCount);

// The extent of the swapchain: the size the application asked for, clamped
// to what the surface allows.
//
// The surface's current extent is NOT the answer, and taking it is what
// froze the window at the size it started at. On X11 the current extent is
// the window's size, which is what the application asked for anyway; on
// Wayland it is the size of the buffer the application last presented, so
// taking it would make every rebuild produce the swapchain that already
// exists - a resize would never take effect, the scene would keep
// rendering into the old targets, and everything outside the old size
// would stay black.
//
// The current extent is the fallback for a request that says nothing: a
// surface whose size the window manager drives and the application does
// not know.
[[nodiscard]] VkExtent2D ComputeSwapchainExtent(uint32_t requestedWidth, uint32_t requestedHeight,
	const VkExtent2D& minExtent, const VkExtent2D& maxExtent, const VkExtent2D& currentExtent);

// The acquire/present result tells the frame loop what to do; flattening
// OUT_OF_DATE into a failure loses the distinction between "rebuild" and
// "the device is gone".
enum class AcquireOutcome : uint8_t { Ok, RebuildNextFrame, Suboptimal, SurfaceLost, Error };
[[nodiscard]] AcquireOutcome ClassifyAcquire(VkResult result);

enum class PresentOutcome : uint8_t { Ok, RebuildNextFrame, Suboptimal, Idle, SurfaceLost, Error };
[[nodiscard]] PresentOutcome ClassifyPresent(VkResult result);

// The acquire-semaphore ring (BRUTAL VulkanBackend: ImageCount + 1
// semaphores): it rotates once per submitted frame, independently of the
// frame index, so a swapchain whose image count changed mid-stream cannot
// hand a semaphore to two frames in flight.
class SemaphoreRing final
{
public:
	SemaphoreRing() = default;
	explicit SemaphoreRing(uint32_t count);

	[[nodiscard]] uint32_t Index() const { return m_Index; }
	[[nodiscard]] uint32_t Count() const { return m_Count; }

	// Advance to the next slot after a submit.
	void Advance();

private:
	uint32_t m_Count = 0;
	uint32_t m_Index = 0;
};

}}
