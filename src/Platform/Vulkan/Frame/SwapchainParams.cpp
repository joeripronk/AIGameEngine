#include "fgpch.h"
#include "Platform/Vulkan/Frame/SwapchainParams.h"

#include "Forge/Core/Log.h"

#include <algorithm>

namespace Forge { namespace Vulkan {

VkSurfaceFormatKHR SelectSurfaceFormat(std::span<const VkSurfaceFormatKHR> candidates, VkSurfaceFormatKHR preferred)
{
	for (const VkSurfaceFormatKHR& candidate : candidates)
	{
		if (candidate.format == preferred.format && candidate.colorSpace == preferred.colorSpace)
			return candidate;
	}

	if (!candidates.empty())
		return candidates[0];

	FG_CORE_ERROR("Swapchain: surface reports no supported formats");
	return VkSurfaceFormatKHR{ VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
}

VkPresentModeKHR SelectPresentMode(std::span<const VkPresentModeKHR> modes, VkPresentModeKHR preferred)
{
	for (const VkPresentModeKHR mode : modes)
	{
		if (mode == preferred)
			return mode;
	}

	// FIFO is the mode every implementation must support; it is the only
	// fallback that is guaranteed to exist.
	return VK_PRESENT_MODE_FIFO_KHR;
}

uint32_t ComputeMinImageCount(VkPresentModeKHR presentMode, uint32_t minImageCount, uint32_t maxImageCount)
{
	uint32_t count = minImageCount;
	if (presentMode == VK_PRESENT_MODE_MAILBOX_KHR && count < 3)
		count = 3;  // mailbox needs a triple to queue frames ahead

	if (maxImageCount > 0 && count > maxImageCount)
		count = maxImageCount;

	return count;
}

VkExtent2D ComputeSwapchainExtent(uint32_t requestedWidth, uint32_t requestedHeight,
	const VkExtent2D& minExtent, const VkExtent2D& maxExtent, const VkExtent2D& currentExtent)
{
	uint32_t width = requestedWidth;
	uint32_t height = requestedHeight;

	// A request that says nothing is answered by the surface's current
	// extent - the one case where the surface knows the size and the
	// application does not. UINT32_MAX is the surface saying "the
	// application decides", which is no answer at all.
	if (width == 0 || height == 0)
	{
		if (currentExtent.width != UINT32_MAX && currentExtent.height != UINT32_MAX)
		{
			width = currentExtent.width;
			height = currentExtent.height;
		}
		else
		{
			width = 1;
			height = 1;
		}
	}

	// Clamp to what the surface allows. A maximum of UINT32_MAX is "no
	// upper bound", and a minimum above the maximum is a surface that says
	// nothing sensible: the minimum wins, because it is the one number the
	// surface cannot do without.
	const uint32_t maxWidth = maxExtent.width == UINT32_MAX ? UINT32_MAX : maxExtent.width;
	const uint32_t maxHeight = maxExtent.height == UINT32_MAX ? UINT32_MAX : maxExtent.height;
	const uint32_t minWidth = std::min(minExtent.width, maxWidth);
	const uint32_t minHeight = std::min(minExtent.height, maxHeight);

	return VkExtent2D{ std::clamp(width, minWidth, maxWidth), std::clamp(height, minHeight, maxHeight) };
}

AcquireOutcome ClassifyAcquire(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS:
		return AcquireOutcome::Ok;
	case VK_EVENT_SET:
		// A *success* result: the image was acquired and the index is
		// valid - the semaphore handed to the acquire was already
		// signaled (leaked by a frame that acquired and never
		// submitted). Treating it as an error discards a valid image,
		// the ring never advances, and every later acquire answers
		// VK_EVENT_SET on the same signaled semaphore: the frame loop
		// hot-spins with zero frames presented. The submit's wait
		// consumes the signal, so the leak heals on this frame.
		return AcquireOutcome::Ok;
	case VK_EVENT_RESET:
		return AcquireOutcome::Error;
	case VK_TIMEOUT:
		return AcquireOutcome::Error;
	case VK_NOT_READY:
		return AcquireOutcome::Error;
	case VK_SUBOPTIMAL_KHR:
		return AcquireOutcome::Suboptimal;
	case VK_ERROR_OUT_OF_DATE_KHR:
		return AcquireOutcome::RebuildNextFrame;
	case VK_ERROR_SURFACE_LOST_KHR:
	case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
		return AcquireOutcome::SurfaceLost;
	default:
		return AcquireOutcome::Error;
	}
}

PresentOutcome ClassifyPresent(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS:
		return PresentOutcome::Ok;
	case VK_SUBOPTIMAL_KHR:
		return PresentOutcome::Suboptimal;
	case VK_TIMEOUT:
		return PresentOutcome::Idle;
	case VK_ERROR_OUT_OF_DATE_KHR:
		return PresentOutcome::RebuildNextFrame;
	case VK_ERROR_SURFACE_LOST_KHR:
	case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
		return PresentOutcome::SurfaceLost;
	default:
		return PresentOutcome::Error;
	}
}

SemaphoreRing::SemaphoreRing(uint32_t count)
	: m_Count(count)
{
}

void SemaphoreRing::Advance()
{
	if (m_Count == 0)
		return;
	m_Index = (m_Index + 1) % m_Count;
}

}}
