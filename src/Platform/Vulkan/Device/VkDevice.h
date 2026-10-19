#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Forge/Core/Base.h"

struct VmaAllocator_T;
typedef VmaAllocator_T* VmaAllocator;

namespace Forge { namespace Vulkan {

class PhysicalDevice;
class Queue;

// The per-device function table (BRUTAL Device.cs: every device-scope entry
// point resolved once through vkGetDeviceProcAddr). Calls through these skip
// the loader's instance dispatch entirely, and the table is what makes the
// renderer independent of any dispatch-loader workaround.
struct DeviceFunctions final
{
	PFN_vkGetDeviceQueue GetDeviceQueue = nullptr;
	PFN_vkDestroyDevice DestroyDevice = nullptr;

	PFN_vkQueueSubmit QueueSubmit = nullptr;
	PFN_vkQueueSubmit2 QueueSubmit2 = nullptr;
	PFN_vkQueuePresentKHR QueuePresentKHR = nullptr;
	PFN_vkQueueWaitIdle QueueWaitIdle = nullptr;
	PFN_vkQueueBindSparse QueueBindSparse = nullptr;

	PFN_vkCreateFence CreateFence = nullptr;
	PFN_vkDestroyFence DestroyFence = nullptr;
	PFN_vkResetFences ResetFences = nullptr;
	PFN_vkWaitForFences WaitForFences = nullptr;
	PFN_vkGetFenceStatus GetFenceStatus = nullptr;

	PFN_vkCreateSemaphore CreateSemaphore = nullptr;
	PFN_vkDestroySemaphore DestroySemaphore = nullptr;

	PFN_vkCreateEvent CreateEvent = nullptr;
	PFN_vkDestroyEvent DestroyEvent = nullptr;
	PFN_vkSetEvent SetEvent = nullptr;
	PFN_vkResetEvent ResetEvent = nullptr;
	PFN_vkGetEventStatus GetEventStatus = nullptr;

	PFN_vkCreateCommandPool CreateCommandPool = nullptr;
	PFN_vkResetCommandPool ResetCommandPool = nullptr;
	PFN_vkDestroyCommandPool DestroyCommandPool = nullptr;
	PFN_vkAllocateCommandBuffers AllocateCommandBuffers = nullptr;
	PFN_vkFreeCommandBuffers FreeCommandBuffers = nullptr;
	PFN_vkBeginCommandBuffer BeginCommandBuffer = nullptr;
	PFN_vkEndCommandBuffer EndCommandBuffer = nullptr;
	PFN_vkResetCommandBuffer ResetCommandBuffer = nullptr;

	PFN_vkCmdPipelineBarrier CmdPipelineBarrier = nullptr;
	PFN_vkCmdPipelineBarrier2 CmdPipelineBarrier2 = nullptr;

	PFN_vkCreateShaderModule CreateShaderModule = nullptr;
	PFN_vkDestroyShaderModule DestroyShaderModule = nullptr;

	PFN_vkCreateBuffer CreateBuffer = nullptr;
	PFN_vkDestroyBuffer DestroyBuffer = nullptr;
	PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = nullptr;
	PFN_vkCreateBufferView CreateBufferView = nullptr;
	PFN_vkDestroyBufferView DestroyBufferView = nullptr;
	PFN_vkGetBufferDeviceAddress GetBufferDeviceAddress = nullptr;

	PFN_vkFlushMappedMemoryRanges FlushMappedMemoryRanges = nullptr;
	PFN_vkInvalidateMappedMemoryRanges InvalidateMappedMemoryRanges = nullptr;

	PFN_vkCreateImage CreateImage = nullptr;
	PFN_vkDestroyImage DestroyImage = nullptr;
	PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = nullptr;
	PFN_vkGetImageSubresourceLayout GetImageSubresourceLayout = nullptr;
	PFN_vkCreateImageView CreateImageView = nullptr;
	PFN_vkDestroyImageView DestroyImageView = nullptr;

	PFN_vkCmdCopyBuffer CmdCopyBuffer = nullptr;
	PFN_vkCmdCopyBufferToImage CmdCopyBufferToImage = nullptr;
	PFN_vkCmdCopyImageToBuffer CmdCopyImageToBuffer = nullptr;
	PFN_vkCmdCopyImage CmdCopyImage = nullptr;
	PFN_vkCmdClearColorImage CmdClearColorImage = nullptr;
	PFN_vkCmdClearDepthStencilImage CmdClearDepthStencilImage = nullptr;

	PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = nullptr;
	PFN_vkDestroyDescriptorSetLayout DestroyDescriptorSetLayout = nullptr;
	PFN_vkCreateDescriptorPool CreateDescriptorPool = nullptr;
	PFN_vkDestroyDescriptorPool DestroyDescriptorPool = nullptr;
	PFN_vkAllocateDescriptorSets AllocateDescriptorSets = nullptr;
	PFN_vkFreeDescriptorSets FreeDescriptorSets = nullptr;
	PFN_vkUpdateDescriptorSets UpdateDescriptorSets = nullptr;

	PFN_vkCreatePipelineLayout CreatePipelineLayout = nullptr;
	PFN_vkDestroyPipelineLayout DestroyPipelineLayout = nullptr;
	PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = nullptr;
	PFN_vkCreateComputePipelines CreateComputePipelines = nullptr;
	PFN_vkDestroyPipeline DestroyPipeline = nullptr;

	PFN_vkCreateRenderPass CreateRenderPass = nullptr;
	PFN_vkDestroyRenderPass DestroyRenderPass = nullptr;
	PFN_vkCreateFramebuffer CreateFramebuffer = nullptr;
	PFN_vkDestroyFramebuffer DestroyFramebuffer = nullptr;
	PFN_vkCmdBeginRenderPass CmdBeginRenderPass = nullptr;
	PFN_vkCmdEndRenderPass CmdEndRenderPass = nullptr;
	PFN_vkCmdNextSubpass CmdNextSubpass = nullptr;

	PFN_vkCmdBindPipeline CmdBindPipeline = nullptr;
	PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = nullptr;
	PFN_vkCmdBindVertexBuffers CmdBindVertexBuffers = nullptr;
	PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer = nullptr;
	PFN_vkCmdDraw CmdDraw = nullptr;
	PFN_vkCmdDrawIndexed CmdDrawIndexed = nullptr;
	PFN_vkCmdDrawIndirect CmdDrawIndirect = nullptr;
	PFN_vkCmdDispatch CmdDispatch = nullptr;
	PFN_vkCmdSetViewport CmdSetViewport = nullptr;
	PFN_vkCmdSetScissor CmdSetScissor = nullptr;
	PFN_vkCmdSetLineWidth CmdSetLineWidth = nullptr;
	PFN_vkCmdSetBlendConstants CmdSetBlendConstants = nullptr;
	PFN_vkCmdSetDepthBias CmdSetDepthBias = nullptr;
	PFN_vkCmdPushConstants CmdPushConstants = nullptr;

	PFN_vkCreateSampler CreateSampler = nullptr;
	PFN_vkDestroySampler DestroySampler = nullptr;

	PFN_vkCreateSwapchainKHR CreateSwapchainKHR = nullptr;
	PFN_vkDestroySwapchainKHR DestroySwapchainKHR = nullptr;
	PFN_vkGetSwapchainImagesKHR GetSwapchainImagesKHR = nullptr;
	PFN_vkAcquireNextImageKHR AcquireNextImageKHR = nullptr;

	PFN_vkSetDebugUtilsObjectNameEXT SetDebugUtilsObjectNameEXT = nullptr;
	PFN_vkCmdBeginDebugUtilsLabelEXT CmdBeginDebugUtilsLabelEXT = nullptr;
	PFN_vkCmdEndDebugUtilsLabelEXT CmdEndDebugUtilsLabelEXT = nullptr;
	PFN_vkCmdInsertDebugUtilsLabelEXT CmdInsertDebugUtilsLabelEXT = nullptr;

	// Resolves every entry through vkGetDeviceProcAddr. A device-scope
	// function that the driver does not export stays null; IsFullyResolved
	// reports it rather than letting a call land on a null pointer.
	void Resolve(::VkDevice device, PFN_vkGetDeviceProcAddr getDeviceProcAddr);

	[[nodiscard]] bool IsFullyResolved() const;
	[[nodiscard]] const char* FirstMissing() const;
};

// Owns the logical device: creation with an explicit extension/queue/feature
// request, the per-device function table, cached queue objects, and the VMA
// allocator every Phase 2 resource allocates through (BRUTAL
// Manual/Classes/Device.cs + VmaAllocator).
class Device final
{
public:
	struct QueueRequest
	{
		uint32_t FamilyIndex = 0;
		std::vector<float> Priorities{ 1.0f };
	};

	struct CreateInfo
	{
		std::vector<const char*> Extensions;
		std::vector<QueueRequest> Queues;
		const void* Features = nullptr;   // pNext feature chain (VkPhysicalDeviceVulkan*Features...)
		bool EnableDeviceAddress = false; // bufferDeviceAddress, chained automatically
	};

	Device() = default;
	~Device();

	Device(const Device&) = delete;
	Device& operator=(const Device&) = delete;

	[[nodiscard]] bool Create(const PhysicalDevice& physicalDevice, const CreateInfo& createInfo);
	void Destroy();

	[[nodiscard]] bool IsNull() const { return m_Handle == VK_NULL_HANDLE; }
	[[nodiscard]] ::VkDevice Handle() const { return m_Handle; }
	[[nodiscard]] const PhysicalDevice& GetPhysicalDevice() const { return *m_PhysicalDevice; }
	[[nodiscard]] const DeviceFunctions& GetFunctions() const { return m_Functions; }

	[[nodiscard]] bool HasExtension(const char* name) const;
	[[nodiscard]] bool IsDeviceAddressEnabled() const { return m_DeviceAddressEnabled; }

	// Cached queue wrappers: the same family/index always answers the same
	// object, so its submit lock is the queue's serialization point
	// process-wide.
	[[nodiscard]] Ref<Queue> GetQueue(uint32_t familyIndex, uint32_t queueIndex);

	// VMA over this device; created once, destroyed before the device.
	[[nodiscard]] bool CreateAllocator();
	[[nodiscard]] VmaAllocator GetAllocator() const { return m_Allocator; }

private:
	const PhysicalDevice* m_PhysicalDevice = nullptr;
	::VkDevice m_Handle = VK_NULL_HANDLE;
	DeviceFunctions m_Functions{};
	std::unordered_set<std::string> m_EnabledExtensions;
	std::unordered_map<uint64_t, Ref<Queue>> m_Queues;
	VmaAllocator m_Allocator = nullptr;
	bool m_DeviceAddressEnabled = false;
};

}}
