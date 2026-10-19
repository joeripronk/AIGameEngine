#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace Forge { namespace Vulkan {

class Device;

// Synchronization objects for one frame (BRUTAL SyncObjects.cs): the acquire
// semaphore, the render-finished semaphore, the per-image fence (created
// signaled), and the one-shot command buffer. CommandPool ownership is an
// explicit flag - when the pool is not ours, Dispose does not free the
// command buffer, because a rebuild that permanently grows a pool it does
// not own is a leak with a rebuild-sized cost.
struct SyncObjects final
{
	Device* Owner = nullptr;

	::VkSemaphore ImageReadySemaphore = VK_NULL_HANDLE;
	::VkSemaphore RenderingFinishedSemaphore = VK_NULL_HANDLE;
	::VkFence Fence = VK_NULL_HANDLE;
	::VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
	::VkCommandPool CommandPool = VK_NULL_HANDLE;  // VK_NULL_HANDLE => not owned
};

// Creates semaphores, a signaled fence and (when a pool is given) a command
// buffer allocated from that pool.
[[nodiscard]] bool CreateSyncObjects(Device& device, SyncObjects& outSync, ::VkCommandPool poolForCommandBuffer);
void DestroySyncObjects(Device& device, SyncObjects& sync);

// One set per swapchain image.
[[nodiscard]] bool CreateSyncObjectsChain(Device& device, std::vector<SyncObjects>& outSyncs, uint32_t count, ::VkCommandPool poolForCommandBuffer);
void DestroySyncObjectsChain(Device& device, std::vector<SyncObjects>& syncs);

}}
