#include <catch2/catch_test_macros.hpp>

#include "Platform/Vulkan/Frame/SwapchainParams.h"

#include <array>

using namespace Forge::Vulkan;

TEST_CASE("Swapchain: surface format prefers the exact match, falls back to the first", "[frame]")
{
	const std::array<VkSurfaceFormatKHR, 3> formats{
		VkSurfaceFormatKHR{ VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
		VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
		VkSurfaceFormatKHR{ VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT },
	};
	const std::span<const VkSurfaceFormatKHR> candidates(formats.data(), formats.size());

	const VkSurfaceFormatKHR preferred{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	const VkSurfaceFormatKHR chosen = SelectSurfaceFormat(candidates, preferred);
	REQUIRE(chosen.format == VK_FORMAT_B8G8R8A8_SRGB);

	// Color space is part of the match: same format, wrong space is not a
	// match, so the fallback (the first candidate) wins.
	const VkSurfaceFormatKHR wrongSpace{ VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT };
	REQUIRE(SelectSurfaceFormat(candidates, wrongSpace).format == VK_FORMAT_R8G8B8A8_UNORM);
}

TEST_CASE("Swapchain: present mode falls back to FIFO, the guaranteed mode", "[frame]")
{
	const std::array<VkPresentModeKHR, 2> modes{ VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR };
	const std::span<const VkPresentModeKHR> available(modes.data(), modes.size());

	REQUIRE(SelectPresentMode(available, VK_PRESENT_MODE_IMMEDIATE_KHR) == VK_PRESENT_MODE_IMMEDIATE_KHR);
	REQUIRE(SelectPresentMode(available, VK_PRESENT_MODE_MAILBOX_KHR) == VK_PRESENT_MODE_FIFO_KHR);

	// FIFO is the answer when only other modes are offered - it is the
	// mode every implementation must support, so the fallback needs no
	// second check.
	const std::array<VkPresentModeKHR, 1> immediateOnly{ VK_PRESENT_MODE_IMMEDIATE_KHR };
	REQUIRE(SelectPresentMode(std::span<const VkPresentModeKHR>(immediateOnly.data(), 1), VK_PRESENT_MODE_MAILBOX_KHR) == VK_PRESENT_MODE_FIFO_KHR);
}

TEST_CASE("Swapchain: image count from the present mode, clamped by the surface", "[frame]")
{
	// Mailbox queues frames ahead: a triple is the floor.
	REQUIRE(ComputeMinImageCount(VK_PRESENT_MODE_MAILBOX_KHR, 2, 0) == 3);
	REQUIRE(ComputeMinImageCount(VK_PRESENT_MODE_MAILBOX_KHR, 4, 0) == 4);

	// FIFO takes the surface minimum.
	REQUIRE(ComputeMinImageCount(VK_PRESENT_MODE_FIFO_KHR, 2, 0) == 2);
	REQUIRE(ComputeMinImageCount(VK_PRESENT_MODE_IMMEDIATE_KHR, 3, 0) == 3);

	// maxImageCount caps it (0 = uncapped); the cap wins even against the
	// mailbox floor - 2 is the most a mailbox on that surface can have.
	REQUIRE(ComputeMinImageCount(VK_PRESENT_MODE_MAILBOX_KHR, 2, 2) == 2);
	REQUIRE(ComputeMinImageCount(VK_PRESENT_MODE_FIFO_KHR, 4, 3) == 3);
	REQUIRE(ComputeMinImageCount(VK_PRESENT_MODE_FIFO_KHR, 2, 8) == 2);
}

TEST_CASE("Frame loop: acquire and present results classify into decisions", "[frame]")
{
	REQUIRE(ClassifyAcquire(VK_SUCCESS) == AcquireOutcome::Ok);
	REQUIRE(ClassifyAcquire(VK_SUBOPTIMAL_KHR) == AcquireOutcome::Suboptimal);
	REQUIRE(ClassifyAcquire(VK_ERROR_OUT_OF_DATE_KHR) == AcquireOutcome::RebuildNextFrame);
	REQUIRE(ClassifyAcquire(VK_ERROR_SURFACE_LOST_KHR) == AcquireOutcome::SurfaceLost);
	REQUIRE(ClassifyAcquire(VK_TIMEOUT) == AcquireOutcome::Error);
	REQUIRE(ClassifyAcquire(VK_ERROR_UNKNOWN) == AcquireOutcome::Error);

	REQUIRE(ClassifyPresent(VK_SUCCESS) == PresentOutcome::Ok);
	REQUIRE(ClassifyPresent(VK_SUBOPTIMAL_KHR) == PresentOutcome::Suboptimal);
	REQUIRE(ClassifyPresent(VK_TIMEOUT) == PresentOutcome::Idle);
	REQUIRE(ClassifyPresent(VK_ERROR_OUT_OF_DATE_KHR) == PresentOutcome::RebuildNextFrame);
	REQUIRE(ClassifyPresent(VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT) == PresentOutcome::SurfaceLost);
	REQUIRE(ClassifyPresent(VK_ERROR_DEVICE_LOST) == PresentOutcome::Error);
}

TEST_CASE("Frame loop: the acquire semaphore ring rotates independently of the frame index", "[frame]")
{
	// ImageCount + 1 slots (BRUTAL VulkanBackend): 3 images, 4 semaphores.
	SemaphoreRing ring(4);
	REQUIRE(ring.Count() == 4);
	REQUIRE(ring.Index() == 0);

	// Six consecutive submits walk the ring and wrap: the frame index
	// (which follows the acquired images, e.g. 0,1,2,0,1,2) never decides
	// which semaphore is used.
	for (uint32_t i = 1; i <= 6; ++i)
	{
		ring.Advance();
		REQUIRE(ring.Index() == i % 4);
	}

	// A ring with no slots cannot rotate.
	SemaphoreRing empty(0);
	empty.Advance();
	REQUIRE(empty.Index() == 0);
}
