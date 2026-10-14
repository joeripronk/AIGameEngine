#include "fgpch.h"
#include "Platform/Vulkan/VulkanVertexBuffer.h"

#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	VulkanVertexBuffer::VulkanVertexBuffer(uint32_t size, const void* data)
		: m_Size(size), m_Layout({ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f })
	{
		nvrhi::BufferDesc desc;
		desc.byteSize = size;
		desc.isVertexBuffer = true;
		desc.debugName = "VulkanVertexBuffer";

		auto device = VulkanRendererAPI::GetStaticDevice();
		m_Buffer = device->createBuffer(desc);

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
		// Simplified - pipeline is stubbed
	}

	void VulkanVertexBuffer::Unbind() const
	{
		// No explicit unbind needed with nvrhi
	}

	void VulkanVertexBuffer::SetData(const void* data, uint32_t size)
	{
		m_Size = size;
		// Data upload will be handled by the rendering pipeline
		(void)data;
	}

	void VulkanVertexBuffer::SetLayout(const std::vector<float>& layout)
	{
		m_Layout = layout;
	}

	Ref<VertexBuffer> VertexBuffer::Create(uint32_t size, const void* data)
	{
		return CreateRef<VulkanVertexBuffer>(size, data);
	}

}

#endif // FG_NO_VULKAN_RUNTIME
