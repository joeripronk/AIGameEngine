#pragma once

#include "Forge/Renderer/VertexBuffer.h"

#include "Forge/Renderer/Renderer.h"

namespace Forge {

	class VulkanVertexBuffer : public VertexBuffer
	{
	public:
		VulkanVertexBuffer(uint32_t size, const void* data);
		~VulkanVertexBuffer() override;

		void Bind() const override;
		void Unbind() const override;

		void SetData(const void* data, uint32_t size) override;

		uint32_t GetSize() const override { return m_Size; }
		const std::vector<float>& GetLayout() const override { return m_Layout; }

		void SetLayout(const std::vector<float>& layout) { m_Layout = layout; }

	private:
		uint32_t m_Size;
		std::vector<float> m_Layout;
	};

}
