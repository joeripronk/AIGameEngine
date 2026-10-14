#pragma once

#include "Forge/Renderer/VertexArray.h"
#include <nvrhi/nvrhi.h>

#include <string>
#include <vector>

namespace Forge {

	class VulkanVertexArray : public VertexArray
	{
	public:
		VulkanVertexArray(const std::vector<Ref<VertexBuffer>>& vertexBuffers, Ref<IndexBuffer> indexBuffer);
		~VulkanVertexArray() override;

		void Bind() const override;
		void Unbind() const override;

		void AddVertexBuffer(const Ref<class VertexBuffer>& vertexBuffer) override;
		void SetIndexBuffer(const Ref<class IndexBuffer>& indexBuffer) override;

		const std::vector<Ref<class VertexBuffer>>& GetVertexBuffers() const override { return m_VertexBuffers; }
		const Ref<class IndexBuffer>& GetIndexBuffer() const override { return m_IndexBuffer; }

		// Builds the nvrhi input layout (and records the resolved byte stride)
		// from the vertex buffer's float-count layout, matched positionally
		// against attributeNames (which must match the vertex shader's `in`
		// variables). nvrhi has no shader reflection, so the caller supplies them.
		bool UpdateLayout(class VulkanShader* vertexShader, const std::vector<std::string>& attributeNames);

		// Records any pending CPU->GPU uploads into the open command list.
		void UploadTo(nvrhi::ICommandList* commandList);

		nvrhi::InputLayoutHandle GetInputLayout() const { return m_InputLayout; }
		uint32_t GetStride() const { return m_Stride; }

		// Vertex/index buffer bindings to place in nvrhi::GraphicsState.
		nvrhi::VertexBufferBinding GetVertexBufferBinding(uint32_t slot = 0) const;
		nvrhi::IndexBufferBinding GetIndexBufferBinding() const;
		bool HasIndexBuffer() const { return m_IndexBuffer != nullptr; }

	private:
		std::vector<Ref<VertexBuffer>> m_VertexBuffers;
		Ref<IndexBuffer> m_IndexBuffer;

		nvrhi::InputLayoutHandle m_InputLayout;
		uint32_t m_Stride = 0;
	};

}
