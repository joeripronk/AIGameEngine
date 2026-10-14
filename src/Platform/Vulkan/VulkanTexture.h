#pragma once

#include "Forge/Renderer/Texture.h"

#include "Forge/Renderer/Renderer.h"
#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

namespace Forge {

	class VulkanTexture : public Texture
	{
	public:
		VulkanTexture(const std::string& filepath);
		VulkanTexture(uint32_t width, uint32_t height, void* data);
		~VulkanTexture() override;

		uint32_t GetWidth() const override { return m_Width; }
		uint32_t GetHeight() const override { return m_Height; }

	private:
		uint32_t m_Width;
		uint32_t m_Height;
		nvrhi::TextureHandle m_Texture;
	};

}
