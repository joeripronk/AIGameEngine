#include "fgpch.h"
#include "Platform/Vulkan/VulkanIndexBuffer.h"

#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	VulkanIndexBuffer::VulkanIndexBuffer(uint32_t count, const void* data)
		: m_Count(count)
	{
		nvrhi::BufferDesc desc;
		desc.byteSize = count * sizeof(uint32_t);
		desc.isIndexBuffer = true;
		desc.debugName = "VulkanIndexBuffer";

		auto device = VulkanRendererAPI::GetStaticDevice();
		m_Buffer = device->createBuffer(desc);

		// Data upload will be handled by the rendering pipeline
		(void)data;
	}

	VulkanIndexBuffer::~VulkanIndexBuffer()
	{
	}

	void VulkanIndexBuffer::Bind() const
	{
		// Simplified - pipeline is stubbed
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

#endif // FG_NO_VULKAN_RUNTIME
