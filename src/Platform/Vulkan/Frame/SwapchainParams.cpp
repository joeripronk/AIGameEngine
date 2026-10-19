#include "fgpch.h"
#include "Platform/Vulkan/Frame/SwapchainParams.h"

#include "Forge/Core/Log.h"

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

AcquireOutcome ClassifyAcquire(VkResult result)
{
	switch (result)
	{
	case VK_SUCCESS:
		return AcquireOutcome::Ok;
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
