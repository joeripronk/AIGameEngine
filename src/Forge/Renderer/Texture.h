#pragma once

#include <string>

namespace Forge {

	class Texture
	{
	public:
		virtual ~Texture() = default;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;

		static Ref<Texture> Create(const std::string& filepath);
		static Ref<Texture> Create(uint32_t width, uint32_t height, void* data);
	};

}
