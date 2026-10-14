#include "fgpch.h"
#include "Platform/Vulkan/VulkanTexture.h"

#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <stb_image.h>

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	VulkanTexture::VulkanTexture(const std::string& filepath)
	{
		int width, height, channels;
		stbi_set_flip_vertically_on_load(true);
		unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
		if (!data)
		{
			FG_CORE_ERROR("Failed to load texture: {0}", filepath);
			m_Width = 0;
			m_Height = 0;
			return;
		}

		m_Width = width;
		m_Height = height;

		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::Format::RGBA8_UNORM;
		desc.isShaderResource = true;
		desc.debugName = filepath;

		auto device = VulkanRendererAPI::GetStaticDevice();
		m_Texture = device->createTexture(desc);

		// Data upload will be handled by the rendering pipeline
		stbi_image_free(data);

		FG_CORE_INFO("Texture created (data upload pending): {0} ({1}x{2})", filepath, width, height);
	}

	VulkanTexture::VulkanTexture(uint32_t width, uint32_t height, void* data)
		: m_Width(width), m_Height(height)
	{
		(void)data;
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::Format::RGBA8_UNORM;
		desc.isShaderResource = true;
		desc.debugName = "VulkanTexture";

		auto device = VulkanRendererAPI::GetStaticDevice();
		m_Texture = device->createTexture(desc);

		// Data upload will be handled by the rendering pipeline
		FG_CORE_INFO("Texture created (data upload pending): {0}x{1}", width, height);
	}

	VulkanTexture::~VulkanTexture()
	{
	}

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
