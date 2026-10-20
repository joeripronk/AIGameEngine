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

	// VK_EVENT_SET is a *success* from vkAcquireNextImageKHR: the image
	// is acquired and the index valid, the semaphore was already
	// signaled. Classifying it as an error discards a valid image,
	// the acquire ring never advances, and every later acquire answers
	// VK_EVENT_SET on the same signaled semaphore - the frame loop
	// hot-spins with zero frames presented (measured on Intel at the
	// Tetris parity run).
	REQUIRE(ClassifyAcquire(VK_EVENT_SET) == AcquireOutcome::Ok);
	REQUIRE(ClassifyAcquire(VK_EVENT_RESET) == AcquireOutcome::Error);

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

	// Six consecutive acquires walk the ring and wrap: the frame index
	// (which follows the acquired images, e.g. 0,1,2,0,1,2) never decides
	// which semaphore is used. Rotating per acquire - not per submit -
	// is what lets a leaked (signaled, never-waited) semaphore come back
	// around and be consumed by a later submit instead of poisoning the
	// ring forever.
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

TEST_CASE("Swapchain: the extent is the size that was asked for, not the surface's current extent", "[frame]")
{
	const VkExtent2D minExtent{ 1, 1 };
	const VkExtent2D maxExtent{ 8192, 8192 };

	// The surface still says the size the swapchain had. Taking that as
	// the answer is what froze the window at the size it started at: every
	// rebuild produced the swapchain that already existed, so a resize
	// never took effect and everything outside the old size stayed black.
	const VkExtent2D staleExtent{ 1600, 900 };
	const VkExtent2D resized = ComputeSwapchainExtent(1900, 1040, minExtent, maxExtent, staleExtent);
	REQUIRE(resized.width == 1900);
	REQUIRE(resized.height == 1040);

	// The request is clamped to what the surface allows.
	const VkExtent2D clamped = ComputeSwapchainExtent(10000, 10000, minExtent, VkExtent2D{ 4096, 4096 }, staleExtent);
	REQUIRE(clamped.width == 4096);
	REQUIRE(clamped.height == 4096);

	// A request that says nothing is answered by the surface - the one
	// case where the surface knows the size and the application does not.
	const VkExtent2D fromSurface = ComputeSwapchainExtent(0, 0, minExtent, maxExtent, staleExtent);
	REQUIRE(fromSurface.width == 1600);
	REQUIRE(fromSurface.height == 900);

	// The surface's answer is clamped like any other: a surface that
	// cannot do the size it reports is raised to the smallest it can.
	const VkExtent2D raised = ComputeSwapchainExtent(0, 0, VkExtent2D{ 1920, 1080 }, maxExtent, staleExtent);
	REQUIRE(raised.width == 1920);
	REQUIRE(raised.height == 1080);

	// A surface that says "the application decides" (UINT32_MAX) has no
	// answer to give, so the request stands.
	const VkExtent2D unbounded = ComputeSwapchainExtent(1200, 700, minExtent, maxExtent, VkExtent2D{ UINT32_MAX, UINT32_MAX });
	REQUIRE(unbounded.width == 1200);
	REQUIRE(unbounded.height == 700);

	// A maximum of UINT32_MAX is no upper bound at all.
	const VkExtent2D huge = ComputeSwapchainExtent(16384, 16384, minExtent, VkExtent2D{ UINT32_MAX, UINT32_MAX }, staleExtent);
	REQUIRE(huge.width == 16384);
	REQUIRE(huge.height == 16384);
}
