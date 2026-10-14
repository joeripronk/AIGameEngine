#pragma once

#include "Forge/Renderer/IndexBuffer.h"

#include "Forge/Renderer/Renderer.h"

namespace Forge {

	class VulkanIndexBuffer : public IndexBuffer
	{
	public:
		VulkanIndexBuffer(uint32_t count, const void* data);
		~VulkanIndexBuffer() override;

		void Bind() const override;
		void Unbind() const override;

		uint32_t GetCount() const override { return m_Count; }

	private:
		uint32_t m_Count;
	};

}
