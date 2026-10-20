#include "fgpch.h"
#include "Platform/Vulkan/Device/VkInstance.h"

#include "Platform/Vulkan/Device/VkPhysicalDevice.h"
#include "Platform/Vulkan/PlatformSurface.h"
#include "Forge/Core/Log.h"

#include <algorithm>

namespace {

	constexpr uint32_t kForgeVulkanApiVersion = VK_API_VERSION_1_3;

}

namespace Forge { namespace Vulkan {

Instance::~Instance()
{
	Destroy();
}

bool Instance::Create(const CreateInfo& createInfo)
{
	if (m_Handle != VK_NULL_HANDLE)
	{
		FG_CORE_WARN("Vulkan: Instance::Create on an already-created instance; Destroy() first");
		return false;
	}

	// The extensions the platform's surface needs: GLFW's on a desktop, the
	// loader's Android surface extension on a phone.
	std::vector<const char*> extensions;
	GetRequiredInstanceExtensions(extensions);

	for (const char* extension : createInfo.Extensions)
		extensions.push_back(extension);
	if (createInfo.EnableDebugMessenger)
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	std::sort(extensions.begin(), extensions.end(), [](const char* a, const char* b) { return std::strcmp(a, b) < 0; });
	extensions.erase(std::unique(extensions.begin(), extensions.end(), [](const char* a, const char* b) { return std::strcmp(a, b) == 0; }),
		extensions.end());

	std::vector<const char*> layers;
	m_HasValidation = false;
	if (createInfo.EnableValidation)
	{
		uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> available(layerCount);
		if (layerCount > 0)
			vkEnumerateInstanceLayerProperties(&layerCount, available.data());

	// The layer name macro is not in every vulkan_core.h vintage; the name
	// itself is fixed by Khronos.
	constexpr const char* kValidationLayer = "VK_LAYER_KHRONOS_validation";
	for (const VkLayerProperties& layer : available)
	{
		if (std::strcmp(layer.layerName, kValidationLayer) == 0)
		{
			layers.push_back(kValidationLayer);
			m_HasValidation = true;
			break;
		}
	}

		if (!m_HasValidation)
			FG_CORE_INFO("Vulkan: validation requested but the layer is not installed");
	}

	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = createInfo.ApplicationName.c_str();
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "Forge";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = kForgeVulkanApiVersion;

	VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
	messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	// Warning and error only. The layer's info severity carries its
	// verbose loader chatter - every layer manifest it looks for, every
	// ICD it registers - which is a hundred lines about nothing at
	// startup. A spec violation is reported as a warning or an error, so
	// those are the two that are worth the console.
	messengerInfo.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	messengerInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	messengerInfo.pfnUserCallback = &Instance::DebugMessengerCallback;

	VkInstanceCreateInfo createInfoVk{};
	createInfoVk.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfoVk.pApplicationInfo = &appInfo;
	createInfoVk.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfoVk.ppEnabledExtensionNames = extensions.data();
	createInfoVk.enabledLayerCount = static_cast<uint32_t>(layers.size());
	createInfoVk.ppEnabledLayerNames = layers.data();
	if (createInfo.EnableDebugMessenger)
		createInfoVk.pNext = &messengerInfo;

	const VkResult result = vkCreateInstance(&createInfoVk, nullptr, &m_Handle);
	if (result != VK_SUCCESS)
	{
		FG_CORE_ERROR("Vulkan: vkCreateInstance failed ({0})", (int)result);
		m_Handle = VK_NULL_HANDLE;
		return false;
	}

	for (const char* extension : extensions)
		m_EnabledExtensions.insert(extension);

	if (createInfo.EnableDebugMessenger)
	{
		// vkCreateDebugUtilsMessengerEXT is an instance-scope entry point:
		// resolved through the instance, never assumed to be exported.
		const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(m_Handle, "vkCreateDebugUtilsMessengerEXT"));
		if (createMessenger)
		{
			if (createMessenger(m_Handle, &messengerInfo, nullptr, &m_Messenger) != VK_SUCCESS)
				FG_CORE_WARN("Vulkan: debug messenger creation failed; validation messages will not be captured");
		}
		else
		{
			FG_CORE_WARN("Vulkan: VK_EXT_debug_utils messenger entry point unavailable");
		}
	}

	// Eager physical device enumeration.
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(m_Handle, &deviceCount, nullptr);
	if (deviceCount == 0)
	{
		FG_CORE_ERROR("Vulkan: no physical device supports Vulkan");
		Destroy();
		return false;
	}

	std::vector<::VkPhysicalDevice> handles(deviceCount);
	vkEnumeratePhysicalDevices(m_Handle, &deviceCount, handles.data());
	m_PhysicalDevices.reserve(handles.size());
	for (::VkPhysicalDevice handle : handles)
		m_PhysicalDevices.push_back(CreateRef<PhysicalDevice>(*this, handle));

	FG_CORE_INFO("Vulkan: instance created with {0} physical device(s), validation {1}",
		deviceCount, m_HasValidation ? "on" : "off");
	return true;
}

void Instance::Destroy()
{
	m_PhysicalDevices.clear();

	if (m_Messenger != VK_NULL_HANDLE)
	{
		const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(m_Handle, "vkDestroyDebugUtilsMessengerEXT"));
		if (destroyMessenger)
			destroyMessenger(m_Handle, m_Messenger, nullptr);
		m_Messenger = VK_NULL_HANDLE;
	}

	if (m_Handle != VK_NULL_HANDLE)
	{
		vkDestroyInstance(m_Handle, nullptr);
		m_Handle = VK_NULL_HANDLE;
	}
	m_EnabledExtensions.clear();
	m_HasValidation = false;
}

PFN_vkVoidFunction Instance::GetProcAddr(const char* name) const
{
	if (m_Handle == VK_NULL_HANDLE)
		return nullptr;
	return vkGetInstanceProcAddr(m_Handle, name);
}

bool Instance::IsExtensionEnabled(const char* name) const
{
	return m_EnabledExtensions.find(name) != m_EnabledExtensions.end();
}

bool Instance::CreateSurface(void* window, ::VkSurfaceKHR& outSurface) const
{
	return CreatePlatformSurface(m_Handle, window, outSurface);
}

VKAPI_ATTR VkBool32 VKAPI_CALL Instance::DebugMessengerCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
	void* userData)
{
	(void)type;
	(void)userData;

	const char* message = (callbackData && callbackData->pMessage) ? callbackData->pMessage : "(no message)";

	switch (severity)
	{
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
		FG_CORE_ERROR("Vulkan validation: {0}", message);
		break;
	case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
		FG_CORE_WARN("Vulkan validation: {0}", message);
		break;
	default:
		FG_CORE_TRACE("Vulkan validation: {0}", message);
		break;
	}

	// The messenger never aborts the call it observed.
	return VK_FALSE;
}

}}
