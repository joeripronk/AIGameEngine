#include "fgpch.h"
#include "Platform/Vulkan/Device/VkQueue.h"

#include "Platform/Vulkan/Device/VkDevice.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

Queue::Queue(const Device& device, uint32_t familyIndex, uint32_t queueIndex, ::VkQueue handle)
	: m_Device(device), m_FamilyIndex(familyIndex), m_QueueIndex(queueIndex), m_Handle(handle)
{
}

bool Queue::Submit(std::span<const ::VkSubmitInfo> submits, ::VkFence fence)
{
	std::lock_guard<std::mutex> lock(m_SubmitLock);

	const VkResult result = m_Device.GetFunctions().QueueSubmit(m_Handle, static_cast<uint32_t>(submits.size()), submits.data(), fence);
	if (result != VK_SUCCESS && result != VK_ERROR_OUT_OF_DATE_KHR)
	{
		FG_CORE_ERROR("Vulkan: vkQueueSubmit failed ({0})", (int)result);
		return false;
	}
	return true;
}

bool Queue::Submit2(std::span<const ::VkSubmitInfo2> submits, ::VkFence fence)
{
	std::lock_guard<std::mutex> lock(m_SubmitLock);

	const VkResult result = m_Device.GetFunctions().QueueSubmit2(m_Handle, static_cast<uint32_t>(submits.size()), submits.data(), fence);
	if (result != VK_SUCCESS && result != VK_ERROR_OUT_OF_DATE_KHR)
	{
		FG_CORE_ERROR("Vulkan: vkQueueSubmit2 failed ({0})", (int)result);
		return false;
	}
	return true;
}

VkResult Queue::PresentKHR(const ::VkPresentInfoKHR& presentInfo)
{
	std::lock_guard<std::mutex> lock(m_SubmitLock);
	return m_Device.GetFunctions().QueuePresentKHR(m_Handle, &presentInfo);
}

bool Queue::WaitIdle()
{
	std::lock_guard<std::mutex> lock(m_SubmitLock);

	const VkResult result = m_Device.GetFunctions().QueueWaitIdle(m_Handle);
	if (result != VK_SUCCESS)
	{
		FG_CORE_ERROR("Vulkan: vkQueueWaitIdle failed ({0})", (int)result);
		return false;
	}
	return true;
}

}}
