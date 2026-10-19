#include "fgpch.h"
#include "Platform/Vulkan/Memory/StagingPool.h"

#include "Platform/Vulkan/Device/VkDevice.h"
#include "Platform/Vulkan/Device/VkQueue.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

StagingPool::StagingPool(IBufferAllocator& allocator, Ref<Queue> queue, uint32_t initialCommandBufferCount)
	: m_Allocator(allocator), m_Device(allocator.GetDevice()), m_Queue(std::move(queue))
{
	if (!m_Queue)
	{
		FG_CORE_ERROR("StagingPool created with no queue; it will not submit");
		return;
	}

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = m_Queue->GetFamilyIndex();

	if (m_Device.GetFunctions().CreateCommandPool(m_Device.Handle(), &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS)
	{
		FG_CORE_ERROR("StagingPool: vkCreateCommandPool failed");
		return;
	}

	if (initialCommandBufferCount > 0)
	{
		VkCommandBufferAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = m_CommandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = initialCommandBufferCount;

		std::vector<::VkCommandBuffer> buffers(initialCommandBufferCount);
		if (m_Device.GetFunctions().AllocateCommandBuffers(m_Device.Handle(), &allocateInfo, buffers.data()) != VK_SUCCESS)
		{
			FG_CORE_ERROR("StagingPool: vkAllocateCommandBuffers failed");
			return;
		}
		m_CommandBuffers = std::move(buffers);
	}
}

StagingPool::~StagingPool()
{
	Dispose();
}

::VkCommandBuffer StagingPool::NextCommandBuffer()
{
	if (m_CommandPool == VK_NULL_HANDLE)
		return VK_NULL_HANDLE;

	if (m_State.CommandBuffersRecorded > m_CommandBuffers.size())
	{
		FG_CORE_ERROR("StagingPool: command buffer cursor passed the allocated count");
		return VK_NULL_HANDLE;
	}

	if (m_State.CommandBuffersRecorded == m_CommandBuffers.size())
	{
		VkCommandBufferAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = m_CommandPool;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;

		::VkCommandBuffer buffer = VK_NULL_HANDLE;
		if (m_Device.GetFunctions().AllocateCommandBuffers(m_Device.Handle(), &allocateInfo, &buffer) != VK_SUCCESS)
		{
			FG_CORE_ERROR("StagingPool: growing the command buffer list failed");
			return VK_NULL_HANDLE;
		}
		m_CommandBuffers.push_back(buffer);
	}

	++m_State.CommandBuffersRecorded;
	return m_CommandBuffers[m_State.CommandBuffersRecorded - 1];
}

Ref<Buffer> StagingPool::AddStagingBuffer(size_t bytes)
{
	if (bytes == 0)
		return nullptr;

	Ref<Buffer> buffer;

	const auto bucket = m_FreeStagingBuffers.find(bytes);
	if (bucket != m_FreeStagingBuffers.end() && !bucket->second.empty())
	{
		buffer = bucket->second.top();
		bucket->second.pop();
	}
	else
	{
		Buffer::CreateInfo createInfo;
		createInfo.Name = "StagingBuffer";
		createInfo.Size = bytes;
		createInfo.Usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		createInfo.RequiredProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

		buffer = CreateRef<Buffer>();
		if (!buffer->Create(m_Allocator, createInfo))
			return nullptr;
	}

	m_StagingBuffers.push_back(buffer);
	++m_State.StagedBuffers;
	return buffer;
}

Ref<Buffer> StagingPool::AddStagingBuffer(const void* data, size_t bytes)
{
	Ref<Buffer> buffer = AddStagingBuffer(bytes);
	if (!buffer || !data)
		return buffer;

	MappedMemory mapped = buffer->Map();
	if (!mapped.IsValid())
	{
		FG_CORE_ERROR("StagingPool: staging buffer map failed; data not copied");
		return nullptr;
	}

	std::memcpy(mapped.Data(), data, bytes);
	mapped.Flush();
	return buffer;
}

bool StagingPool::Submit(std::span<const ::VkSemaphore> waitSemaphores,
	std::span<const VkPipelineStageFlags> waitDstStageMask,
	std::span<const ::VkSemaphore> signalSemaphores)
{
	if (m_State.PendingCommandBuffers() == 0)
		return true;
	if (!m_Queue || m_CommandPool == VK_NULL_HANDLE)
		return false;

	::VkFence fence = VK_NULL_HANDLE;
	if (!m_FreeFences.empty())
	{
		fence = m_FreeFences.top();
		m_FreeFences.pop();
	}
	else
	{
		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		if (m_Device.GetFunctions().CreateFence(m_Device.Handle(), &fenceInfo, nullptr, &fence) != VK_SUCCESS)
		{
			FG_CORE_ERROR("StagingPool: vkCreateFence failed");
			return false;
		}
	}
	{
		FG_CORE_ERROR("StagingPool: vkCreateFence failed");
		return false;
	}

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
	submitInfo.pWaitSemaphores = waitSemaphores.data();
	submitInfo.pWaitDstStageMask = waitDstStageMask.data();
	submitInfo.commandBufferCount = m_State.PendingCommandBuffers();
	submitInfo.pCommandBuffers = m_CommandBuffers.data() + m_State.Submitted;
	submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
	submitInfo.pSignalSemaphores = signalSemaphores.data();

	if (!m_Queue->Submit(std::span<const VkSubmitInfo>(&submitInfo, 1), fence))
		return false;

	m_Fences.push_back(fence);
	// The state bookkeeping cannot fail here: the pending count was checked
	// above and the submit succeeded.
	(void)m_State.Submit();
	return true;
}

bool StagingPool::WaitFor(int64_t timeoutNanos)
{
	if (m_Fences.empty())
		return true;

	const VkResult result = m_Device.GetFunctions().WaitForFences(
		m_Device.Handle(), static_cast<uint32_t>(m_Fences.size()), m_Fences.data(), VK_TRUE, timeoutNanos);
	if (result != VK_SUCCESS)
		return false;

	// Signalled: reset the fences to the free pool and reclaim the staged
	// buffers behind submitted work.
	m_Device.GetFunctions().ResetFences(m_Device.Handle(), static_cast<uint32_t>(m_Fences.size()), m_Fences.data());
	for (::VkFence fence : m_Fences)
		m_FreeFences.push(fence);
	m_Fences.clear();

	const uint32_t reclaimCount = m_State.SubmittedStagedBuffers;
	for (uint32_t i = 0; i < reclaimCount; ++i)
	{
		const size_t size = m_StagingBuffers[i]->Size();
		auto& bucket = m_FreeStagingBuffers[size];
		bucket.push(m_StagingBuffers[i]);
	}
	m_StagingBuffers.erase(m_StagingBuffers.begin(), m_StagingBuffers.begin() + reclaimCount);

	(void)m_State.Reclaim();

	// The pool resets as a whole, so only rewind once nothing is left
	// recorded.
	if (m_State.CanRewind())
	{
		m_Device.GetFunctions().ResetCommandPool(m_Device.Handle(), m_CommandPool, 0);
		m_State.Rewind();
	}

	return true;
}

bool StagingPool::Wait()
{
	return WaitFor(-1);
}

void StagingPool::Dispose()
{
	if (m_Disposed)
		return;
	m_Disposed = true;

	// Teardown: whatever is pending is flushed and waited on; failures are
	// logged by the calls themselves.
	(void)Submit();
	(void)Wait();

	if (m_CommandPool != VK_NULL_HANDLE)
	{
		m_Device.GetFunctions().DestroyCommandPool(m_Device.Handle(), m_CommandPool, nullptr);
		m_CommandPool = VK_NULL_HANDLE;
	}
	m_CommandBuffers.clear();

	for (Ref<Buffer>& buffer : m_StagingBuffers)
		buffer->Dispose();
	m_StagingBuffers.clear();
	for (auto& bucket : m_FreeStagingBuffers)
	{
		while (!bucket.second.empty())
		{
			bucket.second.top()->Dispose();
			bucket.second.pop();
		}
	}
	m_FreeStagingBuffers.clear();

	for (::VkFence fence : m_Fences)
		m_Device.GetFunctions().DestroyFence(m_Device.Handle(), fence, nullptr);
	m_Fences.clear();
	while (!m_FreeFences.empty())
	{
		m_Device.GetFunctions().DestroyFence(m_Device.Handle(), m_FreeFences.top(), nullptr);
		m_FreeFences.pop();
	}
}

}}
