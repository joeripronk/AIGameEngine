#include "fgpch.h"
#include "Platform/Vulkan/VulkanIndexBuffer.h"

#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <cstring>

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	VulkanIndexBuffer::VulkanIndexBuffer(uint32_t count, const void* data)
		: m_Count(count)
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
		{
			FG_CORE_ERROR("VulkanIndexBuffer: renderer device not initialized");
			return;
		}

		nvrhi::BufferDesc desc;
		desc.byteSize = (uint64_t)count * sizeof(uint32_t);
		desc.isIndexBuffer = true;
		desc.debugName = "VulkanIndexBuffer";
		desc.keepInitialState = true;
		desc.initialState = nvrhi::ResourceStates::IndexBuffer;

		m_Buffer = device->createBuffer(desc);

		if (data && count > 0)
		{
			const uint32_t* indices = static_cast<const uint32_t*>(data);
			m_StagingData.assign(indices, indices + count);
			m_PendingUpload = true;
		}
	}

	VulkanIndexBuffer::~VulkanIndexBuffer()
	{
	}

	void VulkanIndexBuffer::Bind() const
	{
		// Bound as part of the recorded graphics state; see VulkanVertexBuffer::Bind.
	}

	void VulkanIndexBuffer::Unbind() const
	{
		// See Bind().
	}

	void VulkanIndexBuffer::UploadTo(nvrhi::ICommandList* commandList)
	{
		if (!m_PendingUpload || !commandList || !m_Buffer || m_StagingData.empty())
			return;

		commandList->writeBuffer(m_Buffer, m_StagingData.data(),
			m_StagingData.size() * sizeof(uint32_t), 0);
		m_PendingUpload = false;
	}

	Ref<IndexBuffer> IndexBuffer::Create(uint32_t count, const void* data)
	{
		return CreateRef<VulkanIndexBuffer>(count, data);
	}

}

#endif // FG_NO_VULKAN_RUNTIME
