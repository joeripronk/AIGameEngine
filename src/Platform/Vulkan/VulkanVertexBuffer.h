#pragma once

#include "Forge/Renderer/VertexBuffer.h"

#include <nvrhi/nvrhi.h>

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

		void SetLayout(const std::vector<float>& layout);

		// Underlying GPU buffer, used to build input layouts and draw calls.
		nvrhi::BufferHandle GetHandle() const { return m_Buffer; }

		// nvrhi records buffer writes into an open command list, so the upload is
		// deferred until the frame's command list is open. Only pending uploads
		// are re-recorded, so repeated frames cost nothing.
		void UploadTo(nvrhi::ICommandList* commandList);
		bool HasPendingUpload() const { return m_PendingUpload; }

	private:
		uint32_t m_Size = 0;
		std::vector<float> m_Layout;
		std::vector<uint8_t> m_StagingData;
		bool m_PendingUpload = false;
		nvrhi::BufferHandle m_Buffer;
	};

}
