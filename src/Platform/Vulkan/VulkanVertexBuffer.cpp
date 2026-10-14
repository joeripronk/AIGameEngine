#include "fgpch.h"
#include "Platform/Vulkan/VulkanVertexBuffer.h"

#include "Forge/Renderer/Renderer.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/common.h>

namespace Forge {

	VulkanVertexBuffer::VulkanVertexBuffer(uint32_t size, const void* data)
		: m_Size(size), m_Layout({ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f })
	{
		nvrhi::BufferDesc desc;
		desc.size = size;
		desc.usage = nvrhi::BufferUsageFlags::Vertex | nvrhi::BufferUsageFlags::CopyDest;
		desc.debugName = "VulkanVertexBuffer";

		m_Buffer = nvrhi::vulkan::CreateBuffer(nvrhi::vulkan::GetDevice(), desc);

		if (data)
		{
			SetData(data, size);
		}
	}

	VulkanVertexBuffer::~VulkanVertexBuffer()
	{
	}

	void VulkanVertexBuffer::Bind() const
	{
		Renderer::s_CommandList->bindVertexBuffers(0, { m_Buffer }, { 0 });
	}

	void VulkanVertexBuffer::Unbind() const
	{
		// No explicit unbind needed with nvrhi
	}

	void VulkanVertexBuffer::SetData(const void* data, uint32_t size)
	{
		m_Size = size;
		Renderer::s_CommandList->copyDataToBuffer(m_Buffer, data, size);
	}

	Ref<VertexBuffer> VertexBuffer::Create(uint32_t size, const void* data)
	{
		return CreateRef<VulkanVertexBuffer>(size, data);
	}

}
