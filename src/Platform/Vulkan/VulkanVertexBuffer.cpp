#include "fgpch.h"
#include "Platform/Vulkan/VulkanVertexBuffer.h"

#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <cstring>

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	VulkanVertexBuffer::VulkanVertexBuffer(uint32_t size, const void* data)
		: m_Size(size), m_Layout({ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f })
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
		{
			FG_CORE_ERROR("VulkanVertexBuffer: renderer device not initialized");
			return;
		}

		nvrhi::BufferDesc desc;
		desc.byteSize = size;
		desc.isVertexBuffer = true;
		desc.debugName = "VulkanVertexBuffer";
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::VertexBuffer;

		m_Buffer = device->createBuffer(desc);

		if (data)
			SetData(data, size);
	}

	VulkanVertexBuffer::~VulkanVertexBuffer()
	{
	}

	void VulkanVertexBuffer::Bind() const
	{
		// nvrhi binds vertex buffers as part of the graphics state recorded
		// into the command list; there is no persistent binding to do here.
	}

	void VulkanVertexBuffer::Unbind() const
	{
		// See Bind().
	}

	void VulkanVertexBuffer::SetData(const void* data, uint32_t size)
	{
		if (!data || size == 0)
			return;

		m_Size = size;

		// Keep a CPU-side copy; the GPU upload is recorded into the frame's
		// command list by UploadTo() (nvrhi requires an open command list).
		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		m_StagingData.assign(bytes, bytes + size);
		m_PendingUpload = true;
	}

	void VulkanVertexBuffer::UploadTo(nvrhi::ICommandList* commandList)
	{
		if (!m_PendingUpload || !commandList || !m_Buffer || m_StagingData.empty())
			return;

		FG_CORE_INFO("VulkanVertexBuffer: uploading {0} bytes (first vertex: {1},{2},{3})",
			m_StagingData.size(),
			*reinterpret_cast<const float*>(m_StagingData.data()),
			*reinterpret_cast<const float*>(m_StagingData.data() + 4),
			*reinterpret_cast<const float*>(m_StagingData.data() + 8));

		commandList->writeBuffer(m_Buffer, m_StagingData.data(), m_StagingData.size(), 0);
		m_PendingUpload = false;
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
