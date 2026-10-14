#include "fgpch.h"
#include "Platform/Vulkan/VulkanIndexBuffer.h"

#include "Forge/Renderer/Renderer.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/common.h>

namespace Forge {

	VulkanIndexBuffer::VulkanIndexBuffer(uint32_t count, const void* data)
		: m_Count(count)
	{
		nvrhi::BufferDesc desc;
		desc.size = count * sizeof(uint32_t);
		desc.usage = nvrhi::BufferUsageFlags::Index | nvrhi::BufferUsageFlags::CopyDest;
		desc.debugName = "VulkanIndexBuffer";

		m_Buffer = nvrhi::vulkan::CreateBuffer(nvrhi::vulkan::GetDevice(), desc);

		if (data)
		{
			Renderer::s_CommandList->copyDataToBuffer(m_Buffer, data, count * sizeof(uint32_t));
		}
	}

	VulkanIndexBuffer::~VulkanIndexBuffer()
	{
	}

	void VulkanIndexBuffer::Bind() const
	{
		Renderer::s_CommandList->bindIndexBuffer(m_Buffer, 0, nvrhi::IndexFormat::Uint32);
	}

	void VulkanIndexBuffer::Unbind() const
	{
		// No explicit unbind needed with nvrhi
	}

	Ref<IndexBuffer> IndexBuffer::Create(uint32_t count, const void* data)
	{
		return CreateRef<VulkanIndexBuffer>(count, data);
	}

}
