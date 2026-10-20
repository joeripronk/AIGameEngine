#include "fgpch.h"

#include "Platform/Vulkan/PlatformSurface.h"

#include "Forge/Core/Log.h"

#if defined(FG_PLATFORM_ANDROID)

	// The system's own window, and the loader's Android surface extension.
	#include <android/native_window.h>
	#include <vulkan/vulkan.h>

#else

	#define GLFW_INCLUDE_NONE
	#include <GLFW/glfw3.h>

#endif

namespace Forge {
	namespace Vulkan {

		void GetRequiredInstanceExtensions(std::vector<const char*>& out)
		{
#if defined(FG_PLATFORM_ANDROID)
			// A surface on a phone is the system's own window, and the
			// extension that makes one is the loader's Android surface
			// extension. There is no GLFW to ask.
			out.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
			out.push_back("VK_KHR_android_surface");
#else
			uint32_t count = 0;
			const char** extensions = glfwGetRequiredInstanceExtensions(&count);
			for (uint32_t i = 0; i < count; ++i)
				out.push_back(extensions[i]);
#endif
		}

		bool CreatePlatformSurface(VkInstance instance, void* nativeWindow, VkSurfaceKHR& outSurface)
		{
			outSurface = VK_NULL_HANDLE;
			if (instance == VK_NULL_HANDLE || !nativeWindow)
			{
				FG_CORE_ERROR("Vulkan: CreatePlatformSurface with no instance or no window");
				return false;
			}

#if defined(FG_PLATFORM_ANDROID)
			// The loader's entry point is fetched by name: the Android
			// surface extension is not in the loader's exported symbols on
			// every version of it.
			auto createAndroidSurface = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
				vkGetInstanceProcAddr(instance, "vkCreateAndroidSurfaceKHR"));
			if (!createAndroidSurface)
			{
				FG_CORE_ERROR("Vulkan: the loader has no vkCreateAndroidSurfaceKHR");
				return false;
			}

			// The window goes in the create info: the Android surface
			// extension is told which window the surface is for.
			VkAndroidSurfaceCreateInfoKHR createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
			createInfo.window = static_cast<ANativeWindow*>(nativeWindow);

			const VkResult result = createAndroidSurface(instance, &createInfo, nullptr, &outSurface);
			if (result != VK_SUCCESS)
			{
				FG_CORE_ERROR("Vulkan: vkCreateAndroidSurfaceKHR failed ({0})", (int)result);
				outSurface = VK_NULL_HANDLE;
				return false;
			}
			return true;
#else
			const VkResult result = glfwCreateWindowSurface(instance,
				static_cast<GLFWwindow*>(nativeWindow), nullptr, &outSurface);
			if (result != VK_SUCCESS)
			{
				FG_CORE_ERROR("Vulkan: glfwCreateWindowSurface failed ({0})", (int)result);
				outSurface = VK_NULL_HANDLE;
				return false;
			}
			return true;
#endif
		}

	}  // namespace Vulkan
}  // namespace Forge
