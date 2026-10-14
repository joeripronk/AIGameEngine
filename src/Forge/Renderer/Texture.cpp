#include "fgpch.h"
#include "Forge/Renderer/Texture.h"

#include "Platform/Vulkan/VulkanTexture.h"

namespace Forge {

	Ref<Texture> Texture::Create(const std::string& filepath)
	{
		return VulkanTexture::Create(filepath);
	}

	Ref<Texture> Texture::Create(uint32_t width, uint32_t height, void* data)
	{
		return VulkanTexture::Create(width, height, data);
	}

}
