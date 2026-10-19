#include "fgpch.h"
#include "Platform/Vulkan/Frame/SyncObjects.h"

#include "Platform/Vulkan/Device/VkDevice.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

bool CreateSyncObjects(Device& device, SyncObjects& outSync, ::VkCommandPool poolForCommandBuffer)
{
	outSync = SyncObjects{};
	outSync.Owner = &device;

	const auto functions = device.GetFunctions();

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	if (functions.CreateSemaphore(device.Handle(), &semaphoreInfo, nullptr, &outSync.ImageReadySemaphore) != VK_SUCCESS)
	{
		FG_CORE_ERROR("SyncObjects: acquire semaphore creation failed");
		DestroySyncObjects(device, outSync);
		return false;
	}
	if (functions.CreateSemaphore(device.Handle(), &semaphoreInfo, nullptr, &outSync.RenderingFinishedSemaphore) != VK_SUCCESS)
	{
		FG_CORE_ERROR("SyncObjects: render-finished semaphore creation failed");
		DestroySyncObjects(device, outSync);
		return false;
	}

	// Created signaled: the first frame must not wait on a fence nobody
	// submitted to.
	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	if (functions.CreateFence(device.Handle(), &fenceInfo, nullptr, &outSync.Fence) != VK_SUCCESS)
	{
		FG_CORE_ERROR("SyncObjects: fence creation failed");
		DestroySyncObjects(device, outSync);
		return false;
	}

	if (poolForCommandBuffer != VK_NULL_HANDLE)
	{
		outSync.CommandPool = poolForCommandBuffer;

		VkCommandBufferAllocateInfo allocateInfo{};
		allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocateInfo.commandPool = poolForCommandBuffer;
		allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocateInfo.commandBufferCount = 1;
		if (functions.AllocateCommandBuffers(device.Handle(), &allocateInfo, &outSync.CommandBuffer) != VK_SUCCESS)
		{
			FG_CORE_ERROR("SyncObjects: command buffer allocation failed");
			DestroySyncObjects(device, outSync);
			return false;
		}
	}

	return true;
}

void DestroySyncObjects(Device& device, SyncObjects& sync)
{
	const auto functions = device.GetFunctions();

	if (sync.CommandBuffer != VK_NULL_HANDLE && sync.CommandPool != VK_NULL_HANDLE)
		functions.FreeCommandBuffers(device.Handle(), sync.CommandPool, 1, &sync.CommandBuffer);
	sync.CommandBuffer = VK_NULL_HANDLE;
	sync.CommandPool = VK_NULL_HANDLE;

	if (sync.Fence != VK_NULL_HANDLE)
		functions.DestroyFence(device.Handle(), sync.Fence, nullptr);
	sync.Fence = VK_NULL_HANDLE;

	if (sync.ImageReadySemaphore != VK_NULL_HANDLE)
		functions.DestroySemaphore(device.Handle(), sync.ImageReadySemaphore, nullptr);
	sync.ImageReadySemaphore = VK_NULL_HANDLE;

	if (sync.RenderingFinishedSemaphore != VK_NULL_HANDLE)
		functions.DestroySemaphore(device.Handle(), sync.RenderingFinishedSemaphore, nullptr);
	sync.RenderingFinishedSemaphore = VK_NULL_HANDLE;

	sync.Owner = nullptr;
}

bool CreateSyncObjectsChain(Device& device, std::vector<SyncObjects>& outSyncs, uint32_t count, ::VkCommandPool poolForCommandBuffer)
{
	outSyncs.clear();
	for (uint32_t i = 0; i < count; ++i)
	{
		SyncObjects sync{};
		if (!CreateSyncObjects(device, sync, poolForCommandBuffer))
		{
			DestroySyncObjectsChain(device, outSyncs);
			return false;
		}
		outSyncs.push_back(sync);
	}
	return true;
}

void DestroySyncObjectsChain(Device& device, std::vector<SyncObjects>& syncs)
{
	for (SyncObjects& sync : syncs)
		DestroySyncObjects(device, sync);
	syncs.clear();
}

}}
