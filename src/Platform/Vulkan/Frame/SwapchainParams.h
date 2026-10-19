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
