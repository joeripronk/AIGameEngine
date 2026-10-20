#include "fgpch.h"
#include "Forge/UI/Font.h"

#include "Forge/Core/FileSystem.h"
#include "Forge/Core/Log.h"

// stb_truetype rasterises a typeface into bitmaps. The implementation is
// compiled into this file only, so the header's own symbols stay local.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace Forge {

	namespace {

		// The characters the atlas holds: the printable ASCII.
		constexpr int kFirstCharacter = 32;
		constexpr int kLastCharacter = 126;
		constexpr int kCharacterCount = kLastCharacter - kFirstCharacter + 1;

		// The atlas is a grid of the glyphs. Sixteen across keeps a glyph's
		// cell wide enough for the widest letter, and the atlas's height
		// follows the rows.
		constexpr int kColumns = 16;

	}  // namespace

	Ref<Font> Font::Load(const std::string& path, int pixelSize)
	{
		if (path.empty() || pixelSize <= 0)
			return nullptr;

		std::vector<uint8_t> fileBytes;
		if (!FileSystem::ReadFile(path, fileBytes) || fileBytes.empty())
		{
			FG_CORE_ERROR("Font: could not read the typeface '{0}'", path);
			return nullptr;
		}

		stbtt_fontinfo info{};
		const int offset = stbtt_GetFontOffsetForIndex(fileBytes.data(), 0);
		if (offset < 0 || !stbtt_InitFont(&info, fileBytes.data(), offset))
		{
			FG_CORE_ERROR("Font: '{0}' is not a typeface the rasteriser understands", path);
			return nullptr;
		}

		// The scale the glyphs are rasterised at.
		const float scale = stbtt_ScaleForPixelHeight(&info, static_cast<float>(pixelSize));

		int ascent = 0;
		int descent = 0;
		stbtt_GetFontVMetrics(&info, &ascent, &descent, nullptr);
		stbtt_GetFontVMetrics(&info, &ascent, &descent, nullptr);

		// The widest glyph decides the cell size, so no glyph is cut off.
		int cellWidth = 1;
		int cellHeight = 1;
		for (int character = kFirstCharacter; character <= kLastCharacter; ++character)
		{
			int width = 0;
			int height = 0;
			int xOffset = 0;
			int yOffset = 0;
			stbtt_GetCodepointBitmap(&info, scale, scale, character, &width, &height, &xOffset, &yOffset);
			cellWidth = std::max(cellWidth, width);
			cellHeight = std::max(cellHeight, height);
		}

		// A little room around each glyph, so the rasteriser's rounding does
		// not clip a stem.
		const int padding = 1;
		const int cellStride = cellWidth + padding * 2;
		const int cellTall = cellHeight + padding * 2;
		const int rows = (kCharacterCount + kColumns - 1) / kColumns;

		auto font = CreateRef<Font>();
		font->m_Width = static_cast<uint32_t>(cellStride * kColumns);
		font->m_Height = static_cast<uint32_t>(cellTall * rows);
		font->m_PixelSize = pixelSize;
		font->m_Ascent = static_cast<float>(ascent) * scale;
		font->m_Descent = static_cast<float>(descent) * scale;
		font->m_Pixels.assign(static_cast<size_t>(font->m_Width) * font->m_Height * 4, 0);
		font->m_Glyphs.assign(kCharacterCount, Glyph{});

		// The glyphs are rasterised into the atlas, white with their coverage
		// in the alpha: the text is the object's colour, and the atlas says
		// how much of it there is.
		for (int character = kFirstCharacter; character <= kLastCharacter; ++character)
		{
			const int index = character - kFirstCharacter;
			const int column = index % kColumns;
			const int row = index / kColumns;

			int width = 0;
			int height = 0;
			int xOffset = 0;
			int yOffset = 0;
			const unsigned char* bitmap = stbtt_GetCodepointBitmap(&info, scale, scale, character,
				&width, &height, &xOffset, &yOffset);

			// How far the pen moves after the glyph.
			int advanceWidth = 0;
			int leftSideBearing = 0;
			stbtt_GetCodepointHMetrics(&info, character, &advanceWidth, &leftSideBearing);

			const int x = column * cellStride + padding;
			const int y = row * cellTall + padding;

			for (int gy = 0; gy < height; ++gy)
			{
				for (int gx = 0; gx < width; ++gx)
				{
					if (!bitmap)
						continue;

					const int coverage = bitmap[gy * width + gx];
					if (coverage <= 0)
						continue;

					const size_t pixel = (static_cast<size_t>(y + gy) * font->m_Width + (x + gx)) * 4;
					if (pixel + 3 >= font->m_Pixels.size())
						continue;

					font->m_Pixels[pixel + 0] = 255;
					font->m_Pixels[pixel + 1] = 255;
					font->m_Pixels[pixel + 2] = 255;
					font->m_Pixels[pixel + 3] = static_cast<uint8_t>(coverage);
				}
			}

			Glyph& glyph = font->m_Glyphs[index];
			glyph.width = static_cast<float>(width);
			glyph.height = static_cast<float>(height);
			// The bitmap's origin is the pen and the baseline: the left
			// bearing is how far the glyph is from the pen, and the vertical
			// one how far its top is above the baseline.
			glyph.bearingX = static_cast<float>(xOffset);
			glyph.bearingY = static_cast<float>(height - yOffset);
			glyph.advance = static_cast<float>(advanceWidth) * scale;
			glyph.u0 = static_cast<float>(x) / static_cast<float>(font->m_Width);
			glyph.v0 = static_cast<float>(y) / static_cast<float>(font->m_Height);
			glyph.u1 = static_cast<float>(x + width) / static_cast<float>(font->m_Width);
			glyph.v1 = static_cast<float>(y + height) / static_cast<float>(font->m_Height);
		}

		FG_CORE_INFO("Font: rasterised '{0}' at {1}px into a {2}x{3} atlas",
			path, pixelSize, font->m_Width, font->m_Height);
		return font;
	}

	bool Font::GetGlyph(char character, Glyph& out) const
	{
		const int index = static_cast<int>(static_cast<unsigned char>(character)) - kFirstCharacter;
		if (index < 0 || index >= static_cast<int>(m_Glyphs.size()))
			return false;

		out = m_Glyphs[index];
		return true;
	}

	float Font::MeasureTextWidth(const std::string& text) const
	{
		float width = 0.0f;
		for (char character : text)
		{
			Glyph glyph{};
			if (GetGlyph(character, glyph))
				width += glyph.advance;
		}
		return width;
	}

}  // namespace Forge
