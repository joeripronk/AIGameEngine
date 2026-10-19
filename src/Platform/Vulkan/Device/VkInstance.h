#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "Forge/Core/Base.h"

struct GLFWwindow;

namespace Forge { namespace Vulkan {

class PhysicalDevice;

// Owns the VkInstance: extension/layer bootstrap (validation enabled when the
// driver offers it), the debug messenger, eager physical device enumeration
// and the instance-level proc address resolution every device-scope function
// table is built from (BRUTAL Manual/Classes/Instance.cs).
class Instance final
{
public:
	struct CreateInfo
	{
		std::string ApplicationName = "Forge";
		std::vector<const char*> Extensions;
		bool EnableValidation = true;    // mandatory if supported
		bool EnableDebugMessenger = true;
	};

	Instance() = default;
	~Instance();

	Instance(const Instance&) = delete;
	Instance& operator=(const Instance&) = delete;

	[[nodiscard]] bool Create(const CreateInfo& createInfo);
	void Destroy();

	[[nodiscard]] bool IsNull() const { return m_Handle == VK_NULL_HANDLE; }
	[[nodiscard]] ::VkInstance Handle() const { return m_Handle; }

	[[nodiscard]] PFN_vkVoidFunction GetProcAddr(const char* name) const;
	[[nodiscard]] bool IsExtensionEnabled(const char* name) const;
	[[nodiscard]] bool HasValidationLayer() const { return m_HasValidation; }

	// Eager enumeration: "which devices exist" is a fact of the instance, and
	// every layer above asks the same question.
	[[nodiscard]] const std::vector<Ref<PhysicalDevice>>& GetPhysicalDevices() const { return m_PhysicalDevices; }

	[[nodiscard]] bool CreateSurface(GLFWwindow* window, ::VkSurfaceKHR& outSurface) const;

private:
	static VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT severity,
		VkDebugUtilsMessageTypeFlagsEXT type,
		const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
		void* userData);

	::VkInstance m_Handle = VK_NULL_HANDLE;
	::VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
	std::unordered_set<std::string> m_EnabledExtensions;
	std::vector<Ref<PhysicalDevice>> m_PhysicalDevices;
	bool m_HasValidation = false;
};

}}
