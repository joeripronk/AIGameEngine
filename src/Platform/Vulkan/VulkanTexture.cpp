#include "fgpch.h"
#include "Platform/Vulkan/VulkanTexture.h"

#include "Forge/Renderer/Renderer.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/common.h>

#include <stb_image.h>

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
		desc.format = nvrhi::TextureFormat::RGBA8_UNorm;
		desc.usage = nvrhi::TextureUsageFlags::ShaderResource;
		desc.debugName = filepath;

		m_Texture = s_Device->createTexture(desc);

		nvrhi::TextureSubresourceData subresourceData;
		subresourceData.pData = data;
		subresourceData.rowPitch = width * channels;
		subresourceData.slicePitch = width * height * channels;

		Renderer::s_CommandList->copyDataToTexture(m_Texture, 0, 0, subresourceData);
		Renderer::s_CommandList->finish();

		nvrhi::ShaderResourceViewDesc srvDesc;
		srvDesc.format = nvrhi::TextureFormat::RGBA8_UNorm;
		srvDesc.texture = m_Texture;
		srvDesc.textureDimension = nvrhi::TextureDimension::Texture2D;
		srvDesc.isTextureArray = false;
		srvDesc.isTextureMS = false;

		m_SRV = s_Device->createShaderResourceView(srvDesc);

		stbi_image_free(data);

		FG_CORE_INFO("Texture loaded: {0} ({1}x{2})", filepath, width, height);
	}

	VulkanTexture::VulkanTexture(uint32_t width, uint32_t height, void* data)
		: m_Width(width), m_Height(height)
	{
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = nvrhi::TextureFormat::RGBA8_UNorm;
		desc.usage = nvrhi::TextureUsageFlags::ShaderResource;
		desc.debugName = "VulkanTexture";

		m_Texture = s_Device->createTexture(desc);

		nvrhi::TextureSubresourceData subresourceData;
		subresourceData.pData = data;
		subresourceData.rowPitch = width * 4;
		subresourceData.slicePitch = width * height * 4;

		Renderer::s_CommandList->copyDataToTexture(m_Texture, 0, 0, subresourceData);
		Renderer::s_CommandList->finish();

		nvrhi::ShaderResourceViewDesc srvDesc;
		srvDesc.format = nvrhi::TextureFormat::RGBA8_UNorm;
		srvDesc.texture = m_Texture;
		srvDesc.textureDimension = nvrhi::TextureDimension::Texture2D;
		srvDesc.isTextureArray = false;
		srvDesc.isTextureMS = false;

		m_SRV = s_Device->createShaderResourceView(srvDesc);
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
