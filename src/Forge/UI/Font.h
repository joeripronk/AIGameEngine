#pragma once

#include "Forge/Core/Base.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Forge {

	class Texture;

	// A typeface, rasterised into an atlas of glyphs.
	//
	// The glyphs are pixels with a coverage, so the text drawn from them is
	// textured: it is blended, and it needs a pipeline of its own, where the
	// built-in vector font's glyphs - solid shapes - did not.
	//
	// The atlas holds the printable ASCII: a label's text is a word or a
	// sentence, and a font that holds every code point would be an atlas
	// nobody could use.
	class Font
	{
	public:
		// Where a glyph is in the atlas, and how far the pen moves after it.
		struct Glyph
		{
			// The glyph's rectangle in the atlas, in the 0 to 1 range.
			float u0 = 0.0f;
			float v0 = 0.0f;
			float u1 = 0.0f;
			float v1 = 0.0f;

			// The glyph's size and its place, in pixels.
			float width = 0.0f;
			float height = 0.0f;
			float bearingX = 0.0f;  // from the pen to the glyph's left
			float bearingY = 0.0f;  // from the baseline to the glyph's bottom
			float advance = 0.0f;   // how far the pen moves
		};

		// Loads a typeface and rasterises the printable ASCII into an atlas.
		//
		// Null when the file cannot be read or is not a typeface: a label
		// with no font falls back to the engine's built-in one, and a font
		// that is not a font is a mistake worth saying out loud.
		static Ref<Font> Load(const std::string& path, int pixelSize = 48);

		// The atlas: the glyphs, white with their coverage in the alpha.
		[[nodiscard]] const std::vector<uint8_t>& GetPixels() const { return m_Pixels; }
		[[nodiscard]] uint32_t GetWidth() const { return m_Width; }
		[[nodiscard]] uint32_t GetHeight() const { return m_Height; }

		// The atlas as a texture, uploaded the first time it is asked for.
		[[nodiscard]] Ref<Texture> GetTexture();

		// The glyph for a character. False for a character the font does
		// not have.
		[[nodiscard]] bool GetGlyph(char character, Glyph& out) const;

		// The distance from the baseline to the top of the tallest glyph, and
		// to the bottom of the lowest.
		[[nodiscard]] float GetAscent() const { return m_Ascent; }
		[[nodiscard]] float GetDescent() const { return m_Descent; }
		[[nodiscard]] float GetLineHeight() const { return m_Ascent - m_Descent; }

		// The size the glyphs were rasterised at.
		[[nodiscard]] int GetPixelSize() const { return m_PixelSize; }

		// The width of a text, in pixels: how far the pen travels.
		[[nodiscard]] float MeasureTextWidth(const std::string& text) const;

	private:
		std::vector<uint8_t> m_Pixels;  // RGBA
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		int m_PixelSize = 0;
		float m_Ascent = 0.0f;
		float m_Descent = 0.0f;

		// The glyphs, indexed by the character minus the first one the
		// atlas holds.
		std::vector<Glyph> m_Glyphs;

		Ref<Texture> m_Texture;
	};

}  // namespace Forge
