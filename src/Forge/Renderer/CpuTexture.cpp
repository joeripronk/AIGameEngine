#include "fgpch.h"
#include "Forge/Renderer/CpuTexture.h"

#include <stb_image.h>

namespace Forge {

	const void* CpuTexture::GetPixelData() const
	{
		if (m_IsHDR)
			return m_Floats.empty() ? nullptr : m_Floats.data();
		return m_Bytes.empty() ? nullptr : m_Bytes.data();
	}

	Ref<CpuTexture> CpuTexture::Load(const std::string& filepath)
	{
		int width = 0, height = 0, channels = 0;

		// Load the whole file into memory so we can inspect its format header and
		// avoid depending on the stb_image file-IO helper variants.
		FILE* file = fopen(filepath.c_str(), "rb");
		if (!file)
		{
			FG_CORE_ERROR("CpuTexture: could not open '{0}'", filepath);
			return nullptr;
		}

		fseek(file, 0, SEEK_END);
		long size = ftell(file);
		fseek(file, 0, SEEK_SET);
		if (size <= 0)
		{
			fclose(file);
			FG_CORE_ERROR("CpuTexture: '{0}' is empty", filepath);
			return nullptr;
		}

		std::vector<unsigned char> buffer((size_t)size);
		size_t read = fread(buffer.data(), 1, (size_t)size, file);
		fclose(file);
		if (read != (size_t)size)
		{
			FG_CORE_ERROR("CpuTexture: failed to read '{0}'", filepath);
			return nullptr;
		}

		bool isHDR = stbi_is_hdr_from_memory(buffer.data(), (int)size) != 0;

		auto texture = CreateRef<CpuTexture>();
		texture->m_IsHDR = isHDR;

		if (isHDR)
		{
			float* data = stbi_loadf_from_memory(buffer.data(), (int)size, &width, &height, &channels, 3);
			if (!data)
			{
				FG_CORE_ERROR("CpuTexture: stb_image failed to decode HDR '{0}': {1}", filepath, stbi_failure_reason());
				return nullptr;
			}

			size_t count = (size_t)width * height * 3;
			texture->m_Floats.assign(data, data + count);
			stbi_image_free(data);
		}
		else
		{
			unsigned char* data = stbi_load_from_memory(buffer.data(), (int)size, &width, &height, &channels, 4);
			if (!data)
			{
				FG_CORE_ERROR("CpuTexture: stb_image failed to decode '{0}': {1}", filepath, stbi_failure_reason());
				return nullptr;
			}

			size_t count = (size_t)width * height * 4;
			texture->m_Bytes.assign(data, data + count);
			stbi_image_free(data);
		}

		texture->m_Width = (uint32_t)width;
		texture->m_Height = (uint32_t)height;
		texture->m_Channels = isHDR ? 3 : 4;
		return texture;
	}

	Ref<CpuTexture> CpuTexture::FromBytes(uint32_t width, uint32_t height, std::vector<uint8_t> bytes)
	{
		auto texture = CreateRef<CpuTexture>();
		texture->m_Width = width;
		texture->m_Height = height;
		texture->m_Channels = 4;
		texture->m_IsHDR = false;
		texture->m_Bytes = std::move(bytes);
		return texture;
	}

}