#pragma once

#include "Forge/Renderer/Texture.h"

#include <cstdint>
#include <vector>

namespace Forge {

	// CPU-side texture that decodes images with stb_image. Supports 8-bit LDR
	// formats (PNG/JPG/TGA/BMP) and 32-bit float Radiance HDR used for IBL.
	class CpuTexture : public Texture
	{
	public:
		uint32_t GetWidth() const override { return m_Width; }
		uint32_t GetHeight() const override { return m_Height; }
		uint32_t GetChannels() const { return m_Channels; }
		bool IsHDR() const { return m_IsHDR; }

		const void* GetPixelData() const;
		std::vector<uint8_t> GetBytes() const { return m_Bytes; }
		const std::vector<float>& GetFloats() const { return m_Floats; }

		// Loads an image from disk. Returns nullptr on failure.
		static Ref<CpuTexture> Load(const std::string& filepath);

		// Wraps raw 8-bit RGBA pixels already in memory.
		static Ref<CpuTexture> FromBytes(uint32_t width, uint32_t height, std::vector<uint8_t> bytes);

	private:
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Channels = 0;
		bool m_IsHDR = false;
		std::vector<uint8_t> m_Bytes;
		std::vector<float> m_Floats;
	};

}