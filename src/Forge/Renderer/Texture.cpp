#include "fgpch.h"
#include "Forge/Renderer/Texture.h"

#include <stb_image.h>

namespace Forge {

	Ref<Texture> Texture::Create(const std::string& filepath)
	{
		// TODO: Implement platform-specific texture creation
		// For now, load the image data
		int width, height, channels;
		stbi_set_flip_vertically_on_load(true);
		unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
		if (!data)
		{
			FG_CORE_ERROR("Failed to load texture: {0}", filepath);
			return nullptr;
		}

		return Create(width, height, data);
	}

	Ref<Texture> Texture::Create(uint32_t width, uint32_t height, void* data)
	{
		// TODO: Create texture resource
		return nullptr;
	}

}
