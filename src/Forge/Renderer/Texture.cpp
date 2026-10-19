#include "fgpch.h"
#include "Forge/Renderer/Texture.h"
#include "Forge/Renderer/CpuTexture.h"

#include <cstring>

#if defined(FG_NO_VULKAN_RUNTIME)
namespace Forge {

	// The GPU-backed factories live in the Vulkan layer (VkTexture.cpp).
	// Headless, a texture is its decoded pixels and nothing else: the CPU
	// decode is device independent, so the hot-reload contract - report the
	// decoded size, report width 0 for a file that does not decode - holds
	// identically with and without a device.
	Ref<Texture> Texture::Create(const std::string& filepath)
	{
		return CpuTexture::Load(filepath);
	}

	Ref<Texture> Texture::Create(uint32_t width, uint32_t height, void* data)
	{
		if (width == 0 || height == 0 || !data)
			return nullptr;
		const size_t bytes = static_cast<size_t>(width) * height * 4;
		std::vector<uint8_t> pixels(bytes);
		std::memcpy(pixels.data(), data, bytes);
		return CpuTexture::FromBytes(width, height, std::move(pixels));
	}

}

#endif





namespace Forge {

	// NOTE: The GPU-backed Texture::Create factories live in the Vulkan platform
	// layer (VkTexture.cpp). Forge::CpuTexture provides device-independent
	// decoding for tests and asset pipelines.

}

