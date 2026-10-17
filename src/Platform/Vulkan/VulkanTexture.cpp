#include "fgpch.h"
#include "Platform/Vulkan/VulkanTexture.h"

#include "Platform/Vulkan/VulkanRendererAPI.h"
#include "Forge/Renderer/CpuTexture.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	VulkanTexture::VulkanTexture(const std::string& filepath)
	{
		// Decode on the CPU (shared, testable path) and upload to the GPU below.
		auto source = CpuTexture::Load(filepath);
		if (!source)
		{
			FG_CORE_ERROR("Failed to load texture: {0}", filepath);
			m_Width = 0;
			m_Height = 0;
			return;
		}

		m_Width = source->GetWidth();
		m_Height = source->GetHeight();

		nvrhi::TextureDesc desc;
		desc.width = m_Width;
		desc.height = m_Height;
		desc.format = source->IsHDR() ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::RGBA8_UNORM;
		desc.isShaderResource = true;
		desc.isRenderTarget = !source->IsHDR();
		desc.debugName = filepath;
		// nvrhi tracks resource states, and a texture with an unknown initial
		// state is rejected at bind time: "Unknown prior state of texture".
		desc.enableAutomaticStateTracking(nvrhi::ResourceStates::ShaderResource);

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
		{
			FG_CORE_WARN("VulkanTexture: device not initialized, keeping CPU data for '{0}'", filepath);
			return;
		}

		m_Texture = device->createTexture(desc);

		// Upload the decoded pixels via a staging texture.
		void* pixels = const_cast<void*>(source->GetPixelData());
		if (pixels && m_Width > 0 && m_Height > 0)
		{
			nvrhi::StagingTextureHandle staging = device->createStagingTexture(desc, nvrhi::CpuAccessMode::Write);
			if (staging)
			{
				size_t rowPitch = 0;
				void* mapped = device->mapStagingTexture(staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Write, &rowPitch);
				if (mapped)
				{
					const size_t rowBytes = (desc.format == nvrhi::Format::RGBA16_FLOAT)
						? (size_t)m_Width * 3 * sizeof(float)
						: (size_t)m_Width * 4;

					for (uint32_t y = 0; y < m_Height; y++)
						memcpy(static_cast<uint8_t*>(mapped) + y * rowPitch, static_cast<uint8_t*>(pixels) + y * rowBytes, rowBytes);

					device->unmapStagingTexture(staging);
				}

				nvrhi::CommandListParameters params;
				nvrhi::CommandListHandle commandList = device->createCommandList(params);
				commandList->open();
				// The desc enables automatic state tracking, so nvrhi inserts the
				// CopyDest barrier for the upload and returns the texture to
				// ShaderResource on its own.
				commandList->copyTexture(m_Texture, nvrhi::TextureSlice(), staging, nvrhi::TextureSlice());
				commandList->close();
				device->executeCommandList(commandList, nvrhi::CommandQueue::Graphics);

				// No Release(): the staging texture is owned by the
				// RefCountPtr handle, whose destructor releases it. Calling
				// ->Release() on top of that drives the refcount negative,
				// the object dies while nvrhi's lifetime tracker still
				// references it, and the crash lands later, in
				// runGarbageCollection, far from the line that caused it.
			}
		}

		FG_CORE_INFO("Texture created: {0} ({1}x{2})", filepath, m_Width, m_Height);
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
		desc.isRenderTarget = true;
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::RenderTarget;
		desc.debugName = "VulkanTexture";

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return;

		m_Texture = device->createTexture(desc);
		FG_CORE_INFO("Texture created: {0}x{1}", width, height);
	}

	VulkanTexture::VulkanTexture(uint32_t width, uint32_t height, void* data, nvrhi::Format format)
		: m_Width(width), m_Height(height)
	{
		(void)data;
		nvrhi::TextureDesc desc;
		desc.width = width;
		desc.height = height;
		desc.format = format;
		desc.isShaderResource = true;
		desc.isRenderTarget = (format == nvrhi::Format::RGBA8_UNORM);
		desc.debugName = "VulkanTexture";

		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
			return;

		m_Texture = device->createTexture(desc);
		FG_CORE_INFO("Texture created: {0}x{1}", width, height);
	}

	VulkanTexture::~VulkanTexture()
	{
	}

	VkImage VulkanTexture::GetVkImage() const
	{
		if (!m_Texture)
			return VK_NULL_HANDLE;

		VkImage image = static_cast<VkImage>(m_Texture->getNativeObject(nvrhi::ObjectTypes::VK_Image));
		return image;
	}

	VkImageView VulkanTexture::GetVkImageView() const
	{
		if (!m_Texture)
			return VK_NULL_HANDLE;

		VkImageView view = static_cast<VkImageView>(m_Texture->getNativeObject(nvrhi::ObjectTypes::VK_ImageView));
		return view;
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