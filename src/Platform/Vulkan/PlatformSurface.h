#pragma once

#include <vulkan/vulkan.h>

#include <vector>

namespace Forge {
	namespace Vulkan {

		// The instance extensions the platform's surface needs, and the
		// surface itself.
		//
		// A desktop window is GLFW's, and GLFW says which extensions the
		// loader needs to make a surface of it. Android has no GLFW at all:
		// the system's own window is the surface, and the loader's Android
		// surface extension is what makes one.
		//
		// The two are behind one name, so the renderer does not branch on
		// the platform - a renderer that knows about phones is a renderer
		// with two answers to every question.
		void GetRequiredInstanceExtensions(std::vector<const char*>& out);

		// The surface for the platform's own window.
		//
		// The window is the platform's own handle: a GLFW window on a
		// desktop, the system's ANativeWindow on a phone.
		[[nodiscard]] bool CreatePlatformSurface(VkInstance instance, void* nativeWindow, VkSurfaceKHR& outSurface);

	}  // namespace Vulkan
}  // namespace Forge
