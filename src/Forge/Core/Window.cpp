#include "fgpch.h"
#include "Forge/Core/Window.h"

#if defined(FG_PLATFORM_WINDOWS)
	#include "Platform/Vulkan/VulkanWindow.h"
#elif defined(FG_PLATFORM_LINUX)
	#include "Platform/Vulkan/VulkanWindow.h"
#elif defined(FG_PLATFORM_MACOS)
	#include "Platform/Vulkan/VulkanWindow.h"
#else
	#error "Unknown platform!"
#endif

namespace Forge {

	Scope<Window> Window::Create(const WindowProps& props)
	{
#if defined(FG_PLATFORM_WINDOWS)
		return CreateScope<VulkanWindow>(props);
#elif defined(FG_PLATFORM_LINUX)
		return CreateScope<VulkanWindow>(props);
#elif defined(FG_PLATFORM_MACOS)
		return CreateScope<VulkanWindow>(props);
#else
		#error "Unknown platform!"
		return nullptr;
#endif
	}

}
