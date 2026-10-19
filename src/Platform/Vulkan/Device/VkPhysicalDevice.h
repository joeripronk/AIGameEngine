#pragma once

#include <vulkan/vulkan.h>

#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace Forge { namespace Vulkan {

class Instance;

// Thin, explicit-ownership wrapper over a physical device handle, with every
// property the layers above query cached eagerly at construction (BRUTAL
// Manual/Classes/PhysicalDevice.cs): extension support, memory types, queue
// families and format features are answers the whole engine shares, and
// enumerating them once removes the question from every call site.
class PhysicalDevice final
{
public:
	PhysicalDevice(const Instance& instance, ::VkPhysicalDevice handle);

	PhysicalDevice(const PhysicalDevice&) = delete;
	PhysicalDevice& operator=(const PhysicalDevice&) = delete;

	[[nodiscard]] ::VkPhysicalDevice Handle() const { return m_Handle; }
	[[nodiscard]] const Instance& GetInstance() const { return m_Instance; }

	[[nodiscard]] const ::VkPhysicalDeviceProperties& GetProperties() const { return m_Properties; }
	[[nodiscard]] const ::VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const { return m_MemoryProperties; }
	[[nodiscard]] const std::vector<::VkQueueFamilyProperties>& GetQueueFamilyProperties() const { return m_QueueFamilies; }

	[[nodiscard]] bool SupportsExtension(const char* name) const;
	[[nodiscard]] const std::unordered_set<std::string>& GetExtensionNames() const { return m_ExtensionNames; }

	// Delegates onto the pure selection rules in DeviceSelection.h, so the
	// ranking that gets unit-tested is the ranking that runs.
	[[nodiscard]] int FindMemoryType(uint32_t memoryTypeBits, VkMemoryPropertyFlags required) const;
	[[nodiscard]] int FindMemoryType(uint32_t memoryTypeBits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred) const;
	[[nodiscard]] int FindQueueFamily(VkQueueFlags required, VkQueueFlags excluded = 0) const;
	[[nodiscard]] VkFormat FindSupportedFormat(std::span<const VkFormat> candidates, VkImageTiling tiling, VkFormatFeatureFlags features) const;

	[[nodiscard]] bool SupportsPresentation(uint32_t queueFamilyIndex, ::VkSurfaceKHR surface) const;

private:
	const Instance& m_Instance;
	::VkPhysicalDevice m_Handle;
	::VkPhysicalDeviceProperties m_Properties{};
	::VkPhysicalDeviceMemoryProperties m_MemoryProperties{};
	std::vector<::VkQueueFamilyProperties> m_QueueFamilies;
	std::unordered_set<std::string> m_ExtensionNames;
};

}}
