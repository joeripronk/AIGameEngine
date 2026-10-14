#pragma once

#include "Forge/Renderer/VertexArray.h"

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

	private:
		std::vector<Ref<VertexBuffer>> m_VertexBuffers;
		Ref<IndexBuffer> m_IndexBuffer;
	};

}
