#include "fgpch.h"
#include "Platform/Vulkan/VkTexture.h"

#include "Forge/Renderer/CpuTexture.h"

#include <cstring>

namespace Forge { namespace Vulkan {

VkTexture::VkTexture(uint32_t width, uint32_t height, std::vector<uint8_t> rgba8)
	: m_Width(width)
	, m_Height(height)
	, m_IsFloat(false)
	, m_Rgba8(std::move(rgba8))
{
}

VkTexture::VkTexture(uint32_t width, uint32_t height, std::vector<float> rgbFloat)
	: m_Width(width)
	, m_Height(height)
	, m_IsFloat(true)
	, m_FloatRgb(std::move(rgbFloat))
{
}

Ref<VkTexture> VkTexture::FromCpu(const CpuTexture& cpu)
{
	if (cpu.IsHDR())
	{
		const std::vector<float> floats = cpu.GetFloats();
		if (floats.empty() || cpu.GetWidth() == 0 || cpu.GetHeight() == 0)
			return nullptr;
		return CreateRef<VkTexture>(cpu.GetWidth(), cpu.GetHeight(), floats);
	}

	std::vector<uint8_t> bytes = cpu.GetBytes();
	if (bytes.empty() || cpu.GetWidth() == 0 || cpu.GetHeight() == 0)
		return nullptr;

	// CpuTexture decodes LDR images with the file's channel count; the
	// upload wants RGBA8, so expand RGB(A-less) sources with a solid alpha.
	const uint32_t pixels = cpu.GetWidth() * cpu.GetHeight();
	if (cpu.GetChannels() == 3 && bytes.size() == pixels * 3)
	{
		std::vector<uint8_t> rgba(pixels * 4);
		for (uint32_t i = 0; i < pixels; ++i)
		{
			rgba[i * 4 + 0] = bytes[i * 3 + 0];
			rgba[i * 4 + 1] = bytes[i * 3 + 1];
			rgba[i * 4 + 2] = bytes[i * 3 + 2];
			rgba[i * 4 + 3] = 255;
		}
		bytes = std::move(rgba);
	}
	else if (cpu.GetChannels() == 1 && bytes.size() == pixels)
	{
		std::vector<uint8_t> rgba(pixels * 4);
		for (uint32_t i = 0; i < pixels; ++i)
		{
			rgba[i * 4 + 0] = bytes[i];
			rgba[i * 4 + 1] = bytes[i];
			rgba[i * 4 + 2] = bytes[i];
			rgba[i * 4 + 3] = 255;
		}
		bytes = std::move(rgba);
	}

	if (bytes.size() != pixels * 4)
		return nullptr;
	return CreateRef<VkTexture>(cpu.GetWidth(), cpu.GetHeight(), std::move(bytes));
}

}}
