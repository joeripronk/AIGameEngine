#pragma once

#include "Forge/Core/Base.h"

#include <vector>

namespace Forge {

	class VertexBuffer
	{
	public:
		virtual ~VertexBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual void SetData(const void* data, uint32_t size) = 0;

		virtual uint32_t GetSize() const = 0;
		virtual const std::vector<float>& GetLayout() const = 0;

		static Ref<VertexBuffer> Create(uint32_t size, const void* data);
	};

}
