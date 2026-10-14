#include "fgpch.h"
#include "Forge/Renderer/Texture.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Platform/Vulkan/VulkanTexture.h"

namespace Forge {

	Ref<Texture> Texture::Create(const std::string& filepath)
	{
		return CreateRef<VulkanTexture>(filepath);
	}

	Ref<Texture> Texture::Create(uint32_t width, uint32_t height, void* data)
	{
		return CreateRef<VulkanTexture>(width, height, data);
	}

}

#endif // FG_NO_VULKAN_RUNTIME
