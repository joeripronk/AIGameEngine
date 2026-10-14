#include "fgpch.h"
#include "Forge/Renderer/Texture.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Platform/Vulkan/VulkanTexture.h"

namespace Forge {

	// NOTE: The GPU-backed Texture::Create factories live in the Vulkan platform
	// layer (VulkanTexture.cpp). Forge::CpuTexture provides device-independent
	// decoding for tests and asset pipelines.

}

#endif // FG_NO_VULKAN_RUNTIME