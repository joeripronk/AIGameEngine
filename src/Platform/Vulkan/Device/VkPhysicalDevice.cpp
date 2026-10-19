#include "fgpch.h"
#include "Platform/Vulkan/Device/VkPhysicalDevice.h"

#include "Platform/Vulkan/Device/DeviceSelection.h"
#include "Platform/Vulkan/Device/VkInstance.h"
#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

PhysicalDevice::PhysicalDevice(const Instance& instance, ::VkPhysicalDevice handle)
	: m_Instance(instance), m_Handle(handle)
{
	vkGetPhysicalDeviceProperties(m_Handle, &m_Properties);
	vkGetPhysicalDeviceMemoryProperties(m_Handle, &m_MemoryProperties);

	uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_Handle, &familyCount, nullptr);
	m_QueueFamilies.resize(familyCount);
	if (familyCount > 0)
		vkGetPhysicalDeviceQueueFamilyProperties(m_Handle, &familyCount, m_QueueFamilies.data());

	uint32_t extensionCount = 0;
	vkEnumerateDeviceExtensionProperties(m_Handle, nullptr, &extensionCount, nullptr);
	std::vector<::VkExtensionProperties> extensions(extensionCount);
	if (extensionCount > 0)
		vkEnumerateDeviceExtensionProperties(m_Handle, nullptr, &extensionCount, extensions.data());

	m_ExtensionNames.reserve(extensionCount);
	for (const ::VkExtensionProperties& extension : extensions)
		m_ExtensionNames.insert(extension.extensionName);
}

bool PhysicalDevice::SupportsExtension(const char* name) const
{
	return m_ExtensionNames.find(name) != m_ExtensionNames.end();
}

int PhysicalDevice::FindMemoryType(uint32_t memoryTypeBits, VkMemoryPropertyFlags required) const
{
	// VkMemoryType{ propertyFlags, heapIndex } matches MemoryTypeDesc by
	// layout (asserted), so the cached array is the census the pure rule
	// ranks.
	const std::span<const MemoryTypeDesc> types(
		reinterpret_cast<const MemoryTypeDesc*>(m_MemoryProperties.memoryTypes),
		m_MemoryProperties.memoryTypeCount);
	return Vulkan::FindMemoryType(memoryTypeBits, required, types);
}

int PhysicalDevice::FindMemoryType(uint32_t memoryTypeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const
{
	const std::span<const MemoryTypeDesc> types(
		reinterpret_cast<const MemoryTypeDesc*>(m_MemoryProperties.memoryTypes),
		m_MemoryProperties.memoryTypeCount);
	return Vulkan::FindMemoryType(memoryTypeBits, required, preferred, types);
}

int PhysicalDevice::FindQueueFamily(VkQueueFlags required, VkQueueFlags excluded) const
{
	// VkQueueFamilyProperties{ queueFlags, queueCount, timestampValidBits,
	// minImageTransferGranularity } - the mirror matches it field for
	// field, so the cached vector is the census the pure rule ranks.
	//
	// The mirror has to match the *element size*, not just the first two
	// fields: the driver's array is contiguous, so a shorter mirror makes
	// the span stride 8 bytes instead of 24 and every family after the
	// first is read from the wrong offset. That is how a transfer-only
	// family got mistaken for the graphics family, and the frame's
	// graphics work ended up submitted to a queue that cannot draw.
	static_assert(sizeof(QueueFamilyDesc) == sizeof(VkQueueFamilyProperties), "QueueFamilyDesc must mirror VkQueueFamilyProperties layout");
	const std::span<const QueueFamilyDesc> families(
		reinterpret_cast<const QueueFamilyDesc*>(m_QueueFamilies.data()),
		m_QueueFamilies.size());
	return Vulkan::FindQueueFamily(required, families, excluded);
}

VkFormat PhysicalDevice::FindSupportedFormat(std::span<const VkFormat> candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const
{
	return Vulkan::FindSupportedFormat(candidates, tiling, features, [this](VkFormat format)
	{
		::VkFormatProperties properties{};
		vkGetPhysicalDeviceFormatProperties(m_Handle, format, &properties);
		return FormatFeatures{ properties.linearTilingFeatures, properties.optimalTilingFeatures };
	});
}

bool PhysicalDevice::SupportsPresentation(uint32_t queueFamilyIndex, ::VkSurfaceKHR surface) const
{
	if (surface == VK_NULL_HANDLE)
		return false;

	VkBool32 supported = VK_FALSE;
	const VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(m_Handle, queueFamilyIndex, surface, &supported);
	if (result != VK_SUCCESS)
	{
		FG_CORE_WARN("Vulkan: surface support query failed ({0})", (int)result);
		return false;
	}
	return supported == VK_TRUE;
}

bool PhysicalDevice::SupportsDynamicRendering() const
{
	// Vulkan 1.3 has dynamic rendering in core, so a 1.3 device always has
	// it. An older device can still expose it through
	// VK_KHR_dynamic_rendering, which the feature struct covers.
	if (m_Properties.apiVersion >= VK_API_VERSION_1_3)
		return true;

	VkPhysicalDeviceDynamicRenderingFeatures features{};
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES;
	VkPhysicalDeviceFeatures2 features2{};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &features;
	vkGetPhysicalDeviceFeatures2(m_Handle, &features2);
	return features.dynamicRendering == VK_TRUE;
}

}}
