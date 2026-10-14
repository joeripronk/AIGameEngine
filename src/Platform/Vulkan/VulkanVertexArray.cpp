#include "fgpch.h"
#include "Platform/Vulkan/VulkanVertexArray.h"
#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include "Platform/Vulkan/VulkanVertexBuffer.h"
#include "Platform/Vulkan/VulkanIndexBuffer.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <string>
#include <vector>

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	namespace {

		// Maps a per-attribute component count to the matching nvrhi format.
		nvrhi::Format FormatForComponents(uint32_t components)
		{
			switch (components)
			{
			case 1:  return nvrhi::Format::R32_FLOAT;
			case 2:  return nvrhi::Format::RG32_FLOAT;
			case 3:  return nvrhi::Format::RGB32_FLOAT;
			case 4:  return nvrhi::Format::RGBA32_FLOAT;
			default: return nvrhi::Format::UNKNOWN;
			}
		}

	}

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

	bool VulkanVertexArray::UpdateLayout(VulkanShader* vertexShader, const std::vector<std::string>& attributeNames)
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device || !vertexShader || !vertexShader->GetVertexShader())
		{
			FG_CORE_ERROR("VulkanVertexArray: cannot build layout (device={0}, shader={1})",
				(bool)device, (bool)vertexShader);
			return false;
		}

		// A single interleaved vertex buffer is expected: the layout list holds
		// the component count of each attribute in declaration order.
		if (m_VertexBuffers.size() != 1)
		{
			FG_CORE_ERROR("VulkanVertexArray: expected exactly 1 vertex buffer, got {0}", m_VertexBuffers.size());
			return false;
		}

		auto vulkanVertexBuffer = std::dynamic_pointer_cast<VulkanVertexBuffer>(m_VertexBuffers.front());
		if (!vulkanVertexBuffer || !vulkanVertexBuffer->GetHandle())
		{
			FG_CORE_ERROR("VulkanVertexArray: vertex buffer is not a valid GPU buffer");
			return false;
		}

		const std::vector<float>& layout = vulkanVertexBuffer->GetLayout();
		if (layout.size() != attributeNames.size())
		{
			FG_CORE_ERROR("VulkanVertexArray: layout has {0} attributes but {1} names were supplied",
				layout.size(), attributeNames.size());
			return false;
		}

		const uint32_t attributeCount = (uint32_t)attributeNames.size();

		// Resolve each attribute's byte offset and the overall vertex stride.
		uint32_t stride = 0;
		std::vector<nvrhi::VertexAttributeDesc> attributes(attributeCount);
		for (uint32_t i = 0; i < attributeCount; ++i)
		{
			const uint32_t components = (uint32_t)layout[i];
			nvrhi::Format format = FormatForComponents(components);
			if (format == nvrhi::Format::UNKNOWN)
			{
				FG_CORE_ERROR("VulkanVertexArray: attribute '{0}' has invalid component count {1}",
					attributeNames[i], components);
				return false;
			}

			attributes[i].name = attributeNames[i];
			attributes[i].format = format;
			attributes[i].arraySize = 1;
			attributes[i].bufferIndex = 0;
			attributes[i].offset = stride;
			attributes[i].elementStride = 0; // filled in below, once known
			attributes[i].isInstanced = false;

			stride += components * sizeof(float);
		}

		for (uint32_t i = 0; i < attributeCount; ++i)
			attributes[i].elementStride = stride;

		nvrhi::InputLayoutHandle layoutHandle = device->createInputLayout(
			attributes.data(), attributeCount, vertexShader->GetVertexShader());

		if (!layoutHandle)
		{
			FG_CORE_ERROR("VulkanVertexArray: createInputLayout failed");
			return false;
		}

		m_InputLayout = layoutHandle;
		m_Stride = stride;
		return true;
	}

	void VulkanVertexArray::UploadTo(nvrhi::ICommandList* commandList)
	{
		for (const auto& vb : m_VertexBuffers)
		{
			if (auto vulkanVertexBuffer = std::dynamic_pointer_cast<VulkanVertexBuffer>(vb))
				vulkanVertexBuffer->UploadTo(commandList);
		}

		if (m_IndexBuffer)
		{
			if (auto vulkanIndexBuffer = std::dynamic_pointer_cast<VulkanIndexBuffer>(m_IndexBuffer))
				vulkanIndexBuffer->UploadTo(commandList);
		}
	}

	nvrhi::VertexBufferBinding VulkanVertexArray::GetVertexBufferBinding(uint32_t slot) const
	{
		nvrhi::VertexBufferBinding binding;

		if (m_VertexBuffers.empty())
			return binding;

		auto vulkanVertexBuffer = std::dynamic_pointer_cast<VulkanVertexBuffer>(m_VertexBuffers.front());
		if (!vulkanVertexBuffer)
			return binding;

		binding.buffer = vulkanVertexBuffer->GetHandle();
		binding.slot = slot;
		binding.offset = 0;
		return binding;
	}

	nvrhi::IndexBufferBinding VulkanVertexArray::GetIndexBufferBinding() const
	{
		nvrhi::IndexBufferBinding binding;

		if (!m_IndexBuffer)
			return binding;

		auto vulkanIndexBuffer = std::dynamic_pointer_cast<VulkanIndexBuffer>(m_IndexBuffer);
		if (!vulkanIndexBuffer)
			return binding;

		binding.buffer = vulkanIndexBuffer->GetHandle();
		binding.offset = 0;
		binding.format = nvrhi::Format::R32_UINT;
		return binding;
	}

	Ref<VertexArray> VertexArray::Create()
	{
		return CreateRef<VulkanVertexArray>(std::vector<Ref<VertexBuffer>>(), nullptr);
	}

}

#endif // FG_NO_VULKAN_RUNTIME
