#include "fgpch.h"
#include "Platform/Vulkan/Device/VkDevice.h"

#include "Platform/Vulkan/Device/VkInstance.h"
#include "Platform/Vulkan/Device/VkPhysicalDevice.h"
#include "Platform/Vulkan/Device/VkQueue.h"
#include "Forge/Core/Log.h"

#include <vk_mem_alloc.h>

namespace Forge { namespace Vulkan {

namespace {

	// Every member name is exactly vk + member, so the table resolves by
	// name concatenation; one macro keeps the list and the resolution from
	// drifting apart.
#define FG_RESOLVE_DEVICE_FUNCTION(fn) \
	fn = reinterpret_cast<decltype(fn)>(getDeviceProcAddr(device, "vk" #fn));

}

void DeviceFunctions::Resolve(::VkDevice device, PFN_vkGetDeviceProcAddr getDeviceProcAddr)
{
	if (!getDeviceProcAddr)
		return;

	FG_RESOLVE_DEVICE_FUNCTION(GetDeviceQueue)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyDevice)
	FG_RESOLVE_DEVICE_FUNCTION(QueueSubmit)
	FG_RESOLVE_DEVICE_FUNCTION(QueueSubmit2)
	FG_RESOLVE_DEVICE_FUNCTION(QueuePresentKHR)
	FG_RESOLVE_DEVICE_FUNCTION(QueueWaitIdle)
	FG_RESOLVE_DEVICE_FUNCTION(QueueBindSparse)
	FG_RESOLVE_DEVICE_FUNCTION(CreateFence)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyFence)
	FG_RESOLVE_DEVICE_FUNCTION(ResetFences)
	FG_RESOLVE_DEVICE_FUNCTION(WaitForFences)
	FG_RESOLVE_DEVICE_FUNCTION(GetFenceStatus)
	FG_RESOLVE_DEVICE_FUNCTION(CreateSemaphore)
	FG_RESOLVE_DEVICE_FUNCTION(DestroySemaphore)
	FG_RESOLVE_DEVICE_FUNCTION(CreateEvent)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyEvent)
	FG_RESOLVE_DEVICE_FUNCTION(SetEvent)
	FG_RESOLVE_DEVICE_FUNCTION(ResetEvent)
	FG_RESOLVE_DEVICE_FUNCTION(GetEventStatus)
	FG_RESOLVE_DEVICE_FUNCTION(CreateCommandPool)
	FG_RESOLVE_DEVICE_FUNCTION(ResetCommandPool)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyCommandPool)
	FG_RESOLVE_DEVICE_FUNCTION(AllocateCommandBuffers)
	FG_RESOLVE_DEVICE_FUNCTION(FreeCommandBuffers)
	FG_RESOLVE_DEVICE_FUNCTION(BeginCommandBuffer)
	FG_RESOLVE_DEVICE_FUNCTION(EndCommandBuffer)
	FG_RESOLVE_DEVICE_FUNCTION(ResetCommandBuffer)
	FG_RESOLVE_DEVICE_FUNCTION(CmdPipelineBarrier)
	FG_RESOLVE_DEVICE_FUNCTION(CmdPipelineBarrier2)
	FG_RESOLVE_DEVICE_FUNCTION(CreateShaderModule)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyShaderModule)
	FG_RESOLVE_DEVICE_FUNCTION(CreateBuffer)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyBuffer)
	FG_RESOLVE_DEVICE_FUNCTION(GetBufferMemoryRequirements)
	FG_RESOLVE_DEVICE_FUNCTION(CreateBufferView)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyBufferView)
	FG_RESOLVE_DEVICE_FUNCTION(GetBufferDeviceAddress)
	FG_RESOLVE_DEVICE_FUNCTION(FlushMappedMemoryRanges)
	FG_RESOLVE_DEVICE_FUNCTION(InvalidateMappedMemoryRanges)
	FG_RESOLVE_DEVICE_FUNCTION(CreateImage)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyImage)
	FG_RESOLVE_DEVICE_FUNCTION(GetImageMemoryRequirements)
	FG_RESOLVE_DEVICE_FUNCTION(GetImageSubresourceLayout)
	FG_RESOLVE_DEVICE_FUNCTION(CreateImageView)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyImageView)
	FG_RESOLVE_DEVICE_FUNCTION(CmdCopyBuffer)
	FG_RESOLVE_DEVICE_FUNCTION(CmdCopyBufferToImage)
	FG_RESOLVE_DEVICE_FUNCTION(CmdCopyImageToBuffer)
	FG_RESOLVE_DEVICE_FUNCTION(CmdCopyImage)
	FG_RESOLVE_DEVICE_FUNCTION(CmdClearColorImage)
	FG_RESOLVE_DEVICE_FUNCTION(CmdClearDepthStencilImage)
	FG_RESOLVE_DEVICE_FUNCTION(CreateDescriptorSetLayout)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyDescriptorSetLayout)
	FG_RESOLVE_DEVICE_FUNCTION(CreateDescriptorPool)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyDescriptorPool)
	FG_RESOLVE_DEVICE_FUNCTION(AllocateDescriptorSets)
	FG_RESOLVE_DEVICE_FUNCTION(FreeDescriptorSets)
	FG_RESOLVE_DEVICE_FUNCTION(UpdateDescriptorSets)
	FG_RESOLVE_DEVICE_FUNCTION(CreatePipelineLayout)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyPipelineLayout)
	FG_RESOLVE_DEVICE_FUNCTION(CreateGraphicsPipelines)
	FG_RESOLVE_DEVICE_FUNCTION(CreateComputePipelines)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyPipeline)
	FG_RESOLVE_DEVICE_FUNCTION(CreateRenderPass)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyRenderPass)
	FG_RESOLVE_DEVICE_FUNCTION(CreateFramebuffer)
	FG_RESOLVE_DEVICE_FUNCTION(DestroyFramebuffer)
	FG_RESOLVE_DEVICE_FUNCTION(CmdBeginRenderPass)
	FG_RESOLVE_DEVICE_FUNCTION(CmdEndRenderPass)
	FG_RESOLVE_DEVICE_FUNCTION(CmdNextSubpass)
	FG_RESOLVE_DEVICE_FUNCTION(CmdBindPipeline)
	FG_RESOLVE_DEVICE_FUNCTION(CmdBindDescriptorSets)
	FG_RESOLVE_DEVICE_FUNCTION(CmdBindVertexBuffers)
	FG_RESOLVE_DEVICE_FUNCTION(CmdBindIndexBuffer)
	FG_RESOLVE_DEVICE_FUNCTION(CmdDraw)
	FG_RESOLVE_DEVICE_FUNCTION(CmdDrawIndexed)
	FG_RESOLVE_DEVICE_FUNCTION(CmdDrawIndirect)
	FG_RESOLVE_DEVICE_FUNCTION(CmdDispatch)
	FG_RESOLVE_DEVICE_FUNCTION(CmdSetViewport)
	FG_RESOLVE_DEVICE_FUNCTION(CmdSetScissor)
	FG_RESOLVE_DEVICE_FUNCTION(CmdSetLineWidth)
	FG_RESOLVE_DEVICE_FUNCTION(CmdSetBlendConstants)
	FG_RESOLVE_DEVICE_FUNCTION(CmdSetDepthBias)
	FG_RESOLVE_DEVICE_FUNCTION(CmdPushConstants)
	FG_RESOLVE_DEVICE_FUNCTION(CreateSampler)
	FG_RESOLVE_DEVICE_FUNCTION(DestroySampler)
	FG_RESOLVE_DEVICE_FUNCTION(CreateSwapchainKHR)
	FG_RESOLVE_DEVICE_FUNCTION(DestroySwapchainKHR)
	FG_RESOLVE_DEVICE_FUNCTION(GetSwapchainImagesKHR)
	FG_RESOLVE_DEVICE_FUNCTION(AcquireNextImageKHR)
	FG_RESOLVE_DEVICE_FUNCTION(SetDebugUtilsObjectNameEXT)
	FG_RESOLVE_DEVICE_FUNCTION(CmdBeginDebugUtilsLabelEXT)
	FG_RESOLVE_DEVICE_FUNCTION(CmdEndDebugUtilsLabelEXT)
	FG_RESOLVE_DEVICE_FUNCTION(CmdInsertDebugUtilsLabelEXT)
}

bool DeviceFunctions::IsFullyResolved() const
{
	return FirstMissing() == nullptr;
}

const char* DeviceFunctions::FirstMissing() const
{
#define FG_CHECK_DEVICE_FUNCTION(fn) if (!fn) return #fn;
	FG_CHECK_DEVICE_FUNCTION(GetDeviceQueue)
	FG_CHECK_DEVICE_FUNCTION(DestroyDevice)
	FG_CHECK_DEVICE_FUNCTION(QueueSubmit)
	FG_CHECK_DEVICE_FUNCTION(QueueSubmit2)
	FG_CHECK_DEVICE_FUNCTION(QueuePresentKHR)
	FG_CHECK_DEVICE_FUNCTION(QueueWaitIdle)
	FG_CHECK_DEVICE_FUNCTION(QueueBindSparse)
	FG_CHECK_DEVICE_FUNCTION(CreateFence)
	FG_CHECK_DEVICE_FUNCTION(DestroyFence)
	FG_CHECK_DEVICE_FUNCTION(ResetFences)
	FG_CHECK_DEVICE_FUNCTION(WaitForFences)
	FG_CHECK_DEVICE_FUNCTION(GetFenceStatus)
	FG_CHECK_DEVICE_FUNCTION(CreateSemaphore)
	FG_CHECK_DEVICE_FUNCTION(DestroySemaphore)
	FG_CHECK_DEVICE_FUNCTION(CreateEvent)
	FG_CHECK_DEVICE_FUNCTION(DestroyEvent)
	FG_CHECK_DEVICE_FUNCTION(SetEvent)
	FG_CHECK_DEVICE_FUNCTION(ResetEvent)
	FG_CHECK_DEVICE_FUNCTION(GetEventStatus)
	FG_CHECK_DEVICE_FUNCTION(CreateCommandPool)
	FG_CHECK_DEVICE_FUNCTION(ResetCommandPool)
	FG_CHECK_DEVICE_FUNCTION(DestroyCommandPool)
	FG_CHECK_DEVICE_FUNCTION(AllocateCommandBuffers)
	FG_CHECK_DEVICE_FUNCTION(FreeCommandBuffers)
	FG_CHECK_DEVICE_FUNCTION(BeginCommandBuffer)
	FG_CHECK_DEVICE_FUNCTION(EndCommandBuffer)
	FG_CHECK_DEVICE_FUNCTION(ResetCommandBuffer)
	FG_CHECK_DEVICE_FUNCTION(CmdPipelineBarrier)
	FG_CHECK_DEVICE_FUNCTION(CmdPipelineBarrier2)
	FG_CHECK_DEVICE_FUNCTION(CreateShaderModule)
	FG_CHECK_DEVICE_FUNCTION(DestroyShaderModule)
	FG_CHECK_DEVICE_FUNCTION(CreateBuffer)
	FG_CHECK_DEVICE_FUNCTION(DestroyBuffer)
	FG_CHECK_DEVICE_FUNCTION(GetBufferMemoryRequirements)
	FG_CHECK_DEVICE_FUNCTION(CreateBufferView)
	FG_CHECK_DEVICE_FUNCTION(DestroyBufferView)
	FG_CHECK_DEVICE_FUNCTION(GetBufferDeviceAddress)
	FG_CHECK_DEVICE_FUNCTION(FlushMappedMemoryRanges)
	FG_CHECK_DEVICE_FUNCTION(InvalidateMappedMemoryRanges)
	FG_CHECK_DEVICE_FUNCTION(CreateImage)
	FG_CHECK_DEVICE_FUNCTION(DestroyImage)
	FG_CHECK_DEVICE_FUNCTION(GetImageMemoryRequirements)
	FG_CHECK_DEVICE_FUNCTION(GetImageSubresourceLayout)
	FG_CHECK_DEVICE_FUNCTION(CreateImageView)
	FG_CHECK_DEVICE_FUNCTION(DestroyImageView)
	FG_CHECK_DEVICE_FUNCTION(CmdCopyBuffer)
	FG_CHECK_DEVICE_FUNCTION(CmdCopyBufferToImage)
	FG_CHECK_DEVICE_FUNCTION(CmdCopyImageToBuffer)
	FG_CHECK_DEVICE_FUNCTION(CmdCopyImage)
	FG_CHECK_DEVICE_FUNCTION(CmdClearColorImage)
	FG_CHECK_DEVICE_FUNCTION(CmdClearDepthStencilImage)
	FG_CHECK_DEVICE_FUNCTION(CreateDescriptorSetLayout)
	FG_CHECK_DEVICE_FUNCTION(DestroyDescriptorSetLayout)
	FG_CHECK_DEVICE_FUNCTION(CreateDescriptorPool)
	FG_CHECK_DEVICE_FUNCTION(DestroyDescriptorPool)
	FG_CHECK_DEVICE_FUNCTION(AllocateDescriptorSets)
	FG_CHECK_DEVICE_FUNCTION(FreeDescriptorSets)
	FG_CHECK_DEVICE_FUNCTION(UpdateDescriptorSets)
	FG_CHECK_DEVICE_FUNCTION(CreatePipelineLayout)
	FG_CHECK_DEVICE_FUNCTION(DestroyPipelineLayout)
	FG_CHECK_DEVICE_FUNCTION(CreateGraphicsPipelines)
	FG_CHECK_DEVICE_FUNCTION(CreateComputePipelines)
	FG_CHECK_DEVICE_FUNCTION(DestroyPipeline)
	FG_CHECK_DEVICE_FUNCTION(CreateRenderPass)
	FG_CHECK_DEVICE_FUNCTION(DestroyRenderPass)
	FG_CHECK_DEVICE_FUNCTION(CreateFramebuffer)
	FG_CHECK_DEVICE_FUNCTION(DestroyFramebuffer)
	FG_CHECK_DEVICE_FUNCTION(CmdBeginRenderPass)
	FG_CHECK_DEVICE_FUNCTION(CmdEndRenderPass)
	FG_CHECK_DEVICE_FUNCTION(CmdNextSubpass)
	FG_CHECK_DEVICE_FUNCTION(CmdBindPipeline)
	FG_CHECK_DEVICE_FUNCTION(CmdBindDescriptorSets)
	FG_CHECK_DEVICE_FUNCTION(CmdBindVertexBuffers)
	FG_CHECK_DEVICE_FUNCTION(CmdBindIndexBuffer)
	FG_CHECK_DEVICE_FUNCTION(CmdDraw)
	FG_CHECK_DEVICE_FUNCTION(CmdDrawIndexed)
	FG_CHECK_DEVICE_FUNCTION(CmdDrawIndirect)
	FG_CHECK_DEVICE_FUNCTION(CmdDispatch)
	FG_CHECK_DEVICE_FUNCTION(CmdSetViewport)
	FG_CHECK_DEVICE_FUNCTION(CmdSetScissor)
	FG_CHECK_DEVICE_FUNCTION(CmdSetLineWidth)
	FG_CHECK_DEVICE_FUNCTION(CmdSetBlendConstants)
	FG_CHECK_DEVICE_FUNCTION(CmdSetDepthBias)
	FG_CHECK_DEVICE_FUNCTION(CmdPushConstants)
	FG_CHECK_DEVICE_FUNCTION(CreateSampler)
	FG_CHECK_DEVICE_FUNCTION(DestroySampler)
	FG_CHECK_DEVICE_FUNCTION(CreateSwapchainKHR)
	FG_CHECK_DEVICE_FUNCTION(DestroySwapchainKHR)
	FG_CHECK_DEVICE_FUNCTION(GetSwapchainImagesKHR)
	FG_CHECK_DEVICE_FUNCTION(AcquireNextImageKHR)
	FG_CHECK_DEVICE_FUNCTION(SetDebugUtilsObjectNameEXT)
	FG_CHECK_DEVICE_FUNCTION(CmdBeginDebugUtilsLabelEXT)
	FG_CHECK_DEVICE_FUNCTION(CmdEndDebugUtilsLabelEXT)
	FG_CHECK_DEVICE_FUNCTION(CmdInsertDebugUtilsLabelEXT)
#undef FG_CHECK_DEVICE_FUNCTION
	return nullptr;
}

Device::~Device()
{
	Destroy();
}

bool Device::Create(const PhysicalDevice& physicalDevice, const CreateInfo& createInfo)
{
	if (m_Handle != VK_NULL_HANDLE)
	{
		FG_CORE_WARN("Vulkan: Device::Create on an already-created device; Destroy() first");
		return false;
	}
	if (physicalDevice.GetInstance().IsNull())
	{
		FG_CORE_ERROR("Vulkan: Device::Create from a physical device of a destroyed instance");
		return false;
	}
	if (createInfo.Queues.empty())
	{
		FG_CORE_ERROR("Vulkan: Device::Create with no queue requested");
		return false;
	}

	std::vector<VkDeviceQueueCreateInfo> queueInfos;
	queueInfos.reserve(createInfo.Queues.size());
	for (const QueueRequest& request : createInfo.Queues)
	{
		VkDeviceQueueCreateInfo queueInfo{};
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = request.FamilyIndex;
		queueInfo.queueCount = static_cast<uint32_t>(request.Priorities.size());
		queueInfo.pQueuePriorities = request.Priorities.data();
		queueInfos.push_back(queueInfo);
	}

	// The feature chain is caller-supplied; the device-address feature the
	// caller asked for is chained on the front so VMA can use it.
	VkPhysicalDeviceBufferDeviceAddressFeatures addressFeatures{};
	addressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
	addressFeatures.bufferDeviceAddress = VK_TRUE;

	VkDeviceCreateInfo deviceInfo{};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
	deviceInfo.pQueueCreateInfos = queueInfos.data();
	deviceInfo.enabledExtensionCount = static_cast<uint32_t>(createInfo.Extensions.size());
	deviceInfo.ppEnabledExtensionNames = createInfo.Extensions.data();

	const void* next = createInfo.Features;
	if (createInfo.EnableDeviceAddress)
	{
		addressFeatures.pNext = const_cast<VkBaseOutStructure*>(static_cast<const VkBaseOutStructure*>(next));
		next = &addressFeatures;
	}
	deviceInfo.pNext = const_cast<void*>(next);  // Vulkan reads pNext, never writes it

	const VkResult result = vkCreateDevice(physicalDevice.Handle(), &deviceInfo, nullptr, &m_Handle);
	if (result != VK_SUCCESS)
	{
		FG_CORE_ERROR("Vulkan: vkCreateDevice failed ({0})", (int)result);
		m_Handle = VK_NULL_HANDLE;
		return false;
	}

	m_PhysicalDevice = &physicalDevice;
	m_DeviceAddressEnabled = createInfo.EnableDeviceAddress;

	for (const char* extension : createInfo.Extensions)
		m_EnabledExtensions.insert(extension);

	const auto getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
		physicalDevice.GetInstance().GetProcAddr("vkGetDeviceProcAddr"));
	m_Functions.Resolve(m_Handle, getDeviceProcAddr);
	if (!m_Functions.IsFullyResolved())
	{
		FG_CORE_ERROR("Vulkan: device function table incomplete - '{0}' is not exported by this driver",
			m_Functions.FirstMissing());
		Destroy();
		return false;
	}

	FG_CORE_INFO("Vulkan: device '{0}' created with {1} queue family request(s)",
		physicalDevice.GetProperties().deviceName, queueInfos.size());
	return true;
}

void Device::Destroy()
{
	m_Queues.clear();

	if (m_Allocator != nullptr)
	{
		vmaDestroyAllocator(m_Allocator);
		m_Allocator = nullptr;
	}

	if (m_Handle != VK_NULL_HANDLE && m_Functions.DestroyDevice)
		m_Functions.DestroyDevice(m_Handle, nullptr);

	m_Handle = VK_NULL_HANDLE;
	m_PhysicalDevice = nullptr;
	m_EnabledExtensions.clear();
	m_DeviceAddressEnabled = false;
	m_Functions = DeviceFunctions{};
}

bool Device::HasExtension(const char* name) const
{
	return m_EnabledExtensions.find(name) != m_EnabledExtensions.end();
}

Ref<Queue> Device::GetQueue(uint32_t familyIndex, uint32_t queueIndex)
{
	if (m_Handle == VK_NULL_HANDLE)
		return nullptr;

	const uint64_t key = (static_cast<uint64_t>(familyIndex) << 32) | queueIndex;
	const auto existing = m_Queues.find(key);
	if (existing != m_Queues.end())
		return existing->second;

	::VkQueue handle = VK_NULL_HANDLE;
	m_Functions.GetDeviceQueue(m_Handle, familyIndex, queueIndex, &handle);
	if (handle == VK_NULL_HANDLE)
	{
		FG_CORE_ERROR("Vulkan: vkGetDeviceQueue returned no handle for family {0} index {1}", familyIndex, queueIndex);
		return nullptr;
	}

	Ref<Queue> queue = CreateRef<Queue>(*this, familyIndex, queueIndex, handle);
	m_Queues.emplace(key, queue);
	return queue;
}

bool Device::CreateAllocator()
{
	if (m_Handle == VK_NULL_HANDLE)
	{
		FG_CORE_ERROR("Vulkan: CreateAllocator on a device that does not exist");
		return false;
	}
	if (m_Allocator != nullptr)
		return true;

	VmaAllocatorCreateInfo info{};
	info.flags = m_DeviceAddressEnabled ? VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT : 0;
	info.vulkanApiVersion = VK_API_VERSION_1_3;
	info.physicalDevice = m_PhysicalDevice->Handle();
	info.device = m_Handle;
	info.instance = m_PhysicalDevice->GetInstance().Handle();

	const VkResult result = vmaCreateAllocator(&info, &m_Allocator);
	if (result != VK_SUCCESS)
	{
		FG_CORE_ERROR("Vulkan: vmaCreateAllocator failed ({0})", (int)result);
		m_Allocator = nullptr;
		return false;
	}
	return true;
}

}}
