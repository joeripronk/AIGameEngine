#include "fgpch.h"
#include "Platform/Vulkan/Frame/FrameLoop.h"

#include "Platform/Vulkan/Device/VkDevice.h"
#include "Platform/Vulkan/Device/VkQueue.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

Ref<FrameLoop> FrameLoop::Create(Device& device, Ref<Queue> queue, Swapchain& swapchain, uint32_t commandBufferCount)
{
	if (!queue || swapchain.IsNull())
	{
		FG_CORE_ERROR("FrameLoop::Create needs a queue and a swapchain");
		return nullptr;
	}

	Ref<FrameLoop> loop = CreateRef<FrameLoop>();
	loop->m_Device = &device;
	loop->m_Queue = std::move(queue);
	loop->m_Swapchain = &swapchain;

	const uint32_t imageCount = static_cast<uint32_t>(swapchain.GetImages().size());
	if (imageCount == 0)
	{
		FG_CORE_ERROR("FrameLoop::Create: swapchain has no images");
		return nullptr;
	}

	const DeviceFunctions& functions = device.GetFunctions();

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = loop->m_Queue->GetFamilyIndex();
	if (functions.CreateCommandPool(device.Handle(), &poolInfo, nullptr, &loop->m_CommandPool) != VK_SUCCESS)
	{
		FG_CORE_ERROR("FrameLoop: command pool creation failed");
		return nullptr;
	}

	if (!CreateSyncObjectsChain(device, loop->m_Frames, imageCount, loop->m_CommandPool))
	{
		functions.DestroyCommandPool(device.Handle(), loop->m_CommandPool, nullptr);
		loop->m_CommandPool = VK_NULL_HANDLE;
		return nullptr;
	}
	for (const SyncObjects& sync : loop->m_Frames)
	{
		loop->m_Fences.push_back(sync.Fence);
		loop->m_RenderFinished.push_back(sync.RenderingFinishedSemaphore);
		loop->m_CommandBuffers.push_back(sync.CommandBuffer);
	}

	// ImageCount + 1 acquire semaphores: the ring rotates once per
	// acquired frame, so the (ImageCount+1)-th acquire reuses the first
	// semaphore exactly when the first frame's submit has consumed it.
	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	loop->m_AcquireSemaphores.resize(imageCount + 1);
	for (::VkSemaphore& semaphore : loop->m_AcquireSemaphores)
	{
		if (functions.CreateSemaphore(device.Handle(), &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
		{
			FG_CORE_ERROR("FrameLoop: acquire semaphore creation failed");
			loop->Destroy();
			return nullptr;
		}
	}
	loop->m_AcquireRing = SemaphoreRing(static_cast<uint32_t>(loop->m_AcquireSemaphores.size()));

	(void)commandBufferCount;  // one command buffer per image, allocated with the chain
	return loop;
}

FrameLoop::~FrameLoop()
{
	Destroy();
}

void FrameLoop::Destroy()
{
	if (m_Destroyed)
		return;
	m_Destroyed = true;

	if (m_Device)
	{
		const DeviceFunctions& functions = m_Device->GetFunctions();

		if (m_CommandPool != VK_NULL_HANDLE)
		{
			// The chain's command buffers belong to this pool; destroying
			// the pool frees them, then the chain frees the rest.
			functions.DestroyCommandPool(m_Device->Handle(), m_CommandPool, nullptr);
			m_CommandPool = VK_NULL_HANDLE;
		}
		for (SyncObjects& sync : m_Frames)
			sync.CommandBuffer = VK_NULL_HANDLE;  // freed with the pool
		DestroySyncObjectsChain(*m_Device, m_Frames);

		for (::VkSemaphore& semaphore : m_AcquireSemaphores)
		{
			if (semaphore != VK_NULL_HANDLE)
				functions.DestroySemaphore(m_Device->Handle(), semaphore, nullptr);
			semaphore = VK_NULL_HANDLE;
		}
	}

	m_AcquireSemaphores.clear();
	m_AcquireRing = SemaphoreRing{};
	m_Swapchain = nullptr;
	m_Queue = nullptr;
	m_Device = nullptr;
}

AcquireOutcome FrameLoop::BeginFrame(uint32_t& outImageIndex)
{
	outImageIndex = 0;
	if (!m_Device || m_CommandPool == VK_NULL_HANDLE || m_Frames.empty())
		return AcquireOutcome::Error;

	const DeviceFunctions& functions = m_Device->GetFunctions();
	const uint32_t ringIndex = m_AcquireRing.Index();

	uint32_t imageIndex = 0;
	// A bounded acquire timeout: an infinite wait on a swapchain that has
	// lost its surface (window closed) never returns, and the app loop
	// blocks there with a quit event queued behind it - the editor spins
	// forever after the window closes. One second is generous for a
	// present queue and finite for a dead surface.
	constexpr uint64_t kAcquireTimeoutNs = 1000ull * 1000ull * 1000ull;
	const AcquireOutcome outcome = m_Swapchain->AcquireNextImage(kAcquireTimeoutNs, m_AcquireSemaphores[ringIndex], imageIndex);
	if (outcome != AcquireOutcome::Ok && outcome != AcquireOutcome::Suboptimal)
		return outcome;

	// The ring rotates on the acquire, not on the submit: a frame that
	// acquires and never submits (a failed fence wait, an EndFrame that
	// bails before Submit) leaks its signaled semaphore, and rotating
	// only on submit would reuse that same signaled semaphore forever -
	// every later acquire answers VK_EVENT_SET and the loop hot-spins
	// with zero frames presented. Rotating per acquire means the leak
	// comes back around ImageCount+1 frames later, the submit's wait
	// consumes it, and the loop heals itself.
	m_AcquireRing.Advance();

	if (imageIndex >= m_Frames.size())
	{
		FG_CORE_ERROR("FrameLoop: image index {0} outside the frame chain", imageIndex);
		return AcquireOutcome::Error;
	}

	SyncObjects& sync = m_Frames[imageIndex];

	// The per-image fence guarantees this image's previous frame is
	// finished before its command buffer is reused.
	if (functions.WaitForFences(m_Device->Handle(), 1, &sync.Fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS)
	{
		FG_CORE_ERROR("FrameLoop: fence wait failed");
		return AcquireOutcome::Error;
	}
	functions.ResetFences(m_Device->Handle(), 1, &sync.Fence);

	// One-shot command buffers: reset the pool, then begin fresh.
	functions.ResetCommandPool(m_Device->Handle(), m_CommandPool, 0);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if (functions.BeginCommandBuffer(sync.CommandBuffer, &beginInfo) != VK_SUCCESS)
	{
		FG_CORE_ERROR("FrameLoop: command buffer begin failed");
		return AcquireOutcome::Error;
	}

	outImageIndex = imageIndex;
	m_CurrentFrame = imageIndex;
	m_CurrentAcquireIndex = ringIndex;
	return outcome;
}

VkCommandBuffer FrameLoop::CurrentCommandBuffer() const
{
	if (m_CurrentFrame >= m_CommandBuffers.size())
		return VK_NULL_HANDLE;
	return m_CommandBuffers[m_CurrentFrame];
}

PresentOutcome FrameLoop::EndFrame(uint32_t imageIndex)
{
	if (!m_Device || imageIndex >= m_CommandBuffers.size())
		return PresentOutcome::Error;

	SyncObjects& sync = m_Frames[imageIndex];

	const DeviceFunctions& functions = m_Device->GetFunctions();

	if (functions.EndCommandBuffer(sync.CommandBuffer) != VK_SUCCESS)
	{
		FG_CORE_ERROR("FrameLoop: command buffer end failed");
		return PresentOutcome::Error;
	}

	const uint32_t ringIndex = m_CurrentAcquireIndex;

	const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &m_AcquireSemaphores[ringIndex];
	submitInfo.pWaitDstStageMask = &waitStage;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &sync.CommandBuffer;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &sync.RenderingFinishedSemaphore;

	if (!m_Queue->Submit(std::span<const VkSubmitInfo>(&submitInfo, 1), sync.Fence))
		return PresentOutcome::Error;

	::VkSwapchainKHR swapchain = m_Swapchain->Handle();
	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &sync.RenderingFinishedSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.pImageIndices = &imageIndex;

	const PresentOutcome outcome = ClassifyPresent(m_Queue->PresentKHR(presentInfo));

	if (outcome == PresentOutcome::RebuildNextFrame)
		FG_CORE_INFO("FrameLoop: present reported OUT_OF_DATE; swapchain rebuild scheduled");

	return outcome;
}

}}
