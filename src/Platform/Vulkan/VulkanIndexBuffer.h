#pragma once

#include "Forge/Renderer/IndexBuffer.h"

#include <nvrhi/nvrhi.h>

#include <vector>

namespace Forge {

	class VulkanIndexBuffer : public IndexBuffer
	{
	public:
		VulkanIndexBuffer(uint32_t count, const void* data);
		~VulkanIndexBuffer() override;

		void Bind() const override;
		void Unbind() const override;

		uint32_t GetCount() const override { return m_Count; }

		// Underlying GPU buffer, used to build draw calls.
		nvrhi::BufferHandle GetHandle() const { return m_Buffer; }

		// Defers the GPU upload until the frame's command list is open
		// (see VulkanVertexBuffer::UploadTo).
		void UploadTo(nvrhi::ICommandList* commandList);
		bool HasPendingUpload() const { return m_PendingUpload; }

	private:
		uint32_t m_Count = 0;
		std::vector<uint32_t> m_StagingData;
		bool m_PendingUpload = false;
		nvrhi::BufferHandle m_Buffer;
	};

}
