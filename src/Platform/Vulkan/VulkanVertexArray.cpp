#include "fgpch.h"
#include "Platform/Vulkan/VulkanVertexArray.h"

#include "Platform/Vulkan/VulkanVertexBuffer.h"
#include "Platform/Vulkan/VulkanIndexBuffer.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/common.h>

namespace Forge {

	VulkanVertexArray::VulkanVertexArray(const std::vector<Ref<VertexBuffer>>& vertexBuffers, Ref<IndexBuffer> indexBuffer)
		: m_VertexBuffers(vertexBuffers), m_IndexBuffer(indexBuffer)
	{
	}

	VulkanVertexArray::~VulkanVertexArray()
	{
	}

	void VulkanVertexArray::Bind() const
	{
		for (const auto& vb : m_VertexBuffers)
		{
			vb->Bind();
		}
		if (m_IndexBuffer)
			m_IndexBuffer->Bind();
	}

	void VulkanVertexArray::Unbind() const
	{
		for (const auto& vb : m_VertexBuffers)
		{
			vb->Unbind();
		}
		if (m_IndexBuffer)
			m_IndexBuffer->Unbind();
	}

	void VulkanVertexArray::AddVertexBuffer(const Ref<class VertexBuffer>& vertexBuffer)
	{
		m_VertexBuffers.push_back(vertexBuffer);
	}

	void VulkanVertexArray::SetIndexBuffer(const Ref<class IndexBuffer>& indexBuffer)
	{
		m_IndexBuffer = indexBuffer;
	}

	Ref<VertexArray> VertexArray::Create()
	{
		return CreateRef<VulkanVertexArray>(std::vector<Ref<VertexBuffer>>(), nullptr);
	}

}
