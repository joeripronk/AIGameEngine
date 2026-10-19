#pragma once

#include "Forge/Renderer/Texture.h"

#include <cstdint>
#include <vector>

namespace Forge {

	class CpuTexture;

	namespace Vulkan {

		// The raw-Vulkan texture (BRUTAL rewrite, Phase 5): the decoded CPU
		// pixels are the asset, and VkRenderer uploads them on first use.
		// LDR pixels are RGBA8; HDR (Radiance) pixels are RGB floats that the
		// upload converts to RGBA16F.
		class VkTexture final : public Texture
		{
		public:
			VkTexture(uint32_t width, uint32_t height, std::vector<uint8_t> rgba8);
			VkTexture(uint32_t width, uint32_t height, std::vector<float> rgbFloat);

			// Wraps a decoded CPU texture, picking the 8-bit or float path.
			static Ref<VkTexture> FromCpu(const CpuTexture& cpu);

			uint32_t GetWidth() const override { return m_Width; }
			uint32_t GetHeight() const override { return m_Height; }

			bool IsFloat() const { return m_IsFloat; }
			const std::vector<uint8_t>& GetRgba8() const { return m_Rgba8; }
			const std::vector<float>& GetFloatRgb() const { return m_FloatRgb; }

		private:
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
			bool m_IsFloat = false;
			std::vector<uint8_t> m_Rgba8;
			std::vector<float> m_FloatRgb;
		};

	}

}
