#include "fgpch.h"
#include "Forge/UI/Label.h"

#include "Forge/Renderer/CpuMesh.h"
#include "Forge/UI/Font.h"

// The engine's font: stb_easy_font's vector font, which is in the public
// domain and needs no asset. The glyphs are quads, so a label is a mesh -
// no texture, no blending, and the same draw path as any other geometry.
#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"

namespace Forge {

	namespace {

		// The font's own line height, in its pixels. The built-in font has
		// one size, and the label's size is how tall the text is in world
		// units.
		constexpr float kBuiltInLineHeight = 12.0f;

	}  // namespace

	Ref<CpuMesh> BuildTextMesh(const std::string& text, float size)
	{
		if (text.empty())
			return nullptr;

		// The font draws quads of six floats: the position and the colour.
		// The colour is dropped: a label is coloured by its material, the
		// same way a cube is.
		std::vector<float> quads;
		quads.resize(4096);

		// The font's spacing is a global, so it is set before every build:
		// a label's width must not depend on the label that was built before
		// it.
		stb_easy_font_spacing(0.0f);

		std::string mutableText = text;
		const int quadCount = stb_easy_font_print(0.0f, 0.0f, mutableText.data(), nullptr,
			quads.data(), static_cast<int>(quads.size() * sizeof(float)));

		if (quadCount <= 0)
			return nullptr;

		// The text is `size` world units tall, and the font is
		// kBuiltInLineHeight pixels tall.
		const float scale = size / kBuiltInLineHeight;

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(static_cast<size_t>(quadCount) * 4);
		indices.reserve(static_cast<size_t>(quadCount) * 6);

		for (int quad = 0; quad < quadCount; ++quad)
		{
			const float* corners = quads.data() + static_cast<size_t>(quad) * 24;

			const uint32_t base = static_cast<uint32_t>(vertices.size());
			for (int corner = 0; corner < 4; ++corner)
			{
				const float x = corners[corner * 4 + 0];
				const float y = corners[corner * 4 + 1];

				Vertex vertex{};
				// The font's y grows downwards; the world's grows upwards.
				vertex.Position = glm::vec3(x * scale, -y * scale, 0.0f);
				vertex.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
				vertex.UV = glm::vec2(0.0f, 0.0f);
				vertex.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
				vertex.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
				vertices.push_back(vertex);
			}

			indices.push_back(base + 0);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			indices.push_back(base + 0);
			indices.push_back(base + 2);
			indices.push_back(base + 3);
		}

		return CreateRef<CpuMesh>(std::move(vertices), std::move(indices));
	}

	Ref<CpuMesh> BuildFontTextMesh(const Ref<Font>& font, const std::string& text, float size)
	{
		if (!font || text.empty())
			return nullptr;

		// The text is `size` world units tall, and the typeface's line is
		// as tall as the font says.
		const float lineHeight = font->GetLineHeight();
		if (lineHeight <= 0.0f)
			return nullptr;

		const float scale = size / lineHeight;

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		// The pen moves along the text; the baseline is the line the glyphs
		// stand on.
		float penX = 0.0f;
		for (char character : text)
		{
			Font::Glyph glyph{};
			if (!font->GetGlyph(character, glyph))
				continue;

			if (glyph.width > 0.0f && glyph.height > 0.0f)
			{
				const float left = (penX + glyph.bearingX) * scale;
				// The font's y grows upwards from the baseline, and the
				// label's does too.
				const float bottom = -glyph.bearingY * scale;
				const float right = left + glyph.width * scale;
				const float top = bottom + glyph.height * scale;

				const uint32_t base = static_cast<uint32_t>(vertices.size());

				const glm::vec3 corners[4] = {
					{ left, bottom, 0.0f },
					{ right, bottom, 0.0f },
					{ right, top, 0.0f },
					{ left, top, 0.0f },
				};
				// The atlas's rows run from the top, and the bottom of the
				// glyph is the bottom of its rectangle in the atlas.
				const glm::vec2 uvs[4] = {
					{ glyph.u0, glyph.v1 },
					{ glyph.u1, glyph.v1 },
					{ glyph.u1, glyph.v0 },
					{ glyph.u0, glyph.v0 },
				};

				for (int corner = 0; corner < 4; ++corner)
				{
					Vertex vertex{};
					vertex.Position = corners[corner];
					vertex.Normal = glm::vec3(0.0f, 0.0f, 1.0f);
					vertex.UV = uvs[corner];
					vertex.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
					vertex.Bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
					vertices.push_back(vertex);
				}

				indices.push_back(base + 0);
				indices.push_back(base + 1);
				indices.push_back(base + 2);
				indices.push_back(base + 0);
				indices.push_back(base + 2);
				indices.push_back(base + 3);
			}

			penX += glyph.advance;
		}

		if (vertices.empty())
			return nullptr;

		return CreateRef<CpuMesh>(std::move(vertices), std::move(indices));
	}

	float MeasureTextWidth(const std::string& text, float size)
	{
		if (text.empty())
			return 0.0f;

		stb_easy_font_spacing(0.0f);
		std::string mutableText = text;
		const float width = static_cast<float>(stb_easy_font_width(mutableText.data()));
		return width * (size / kBuiltInLineHeight);
	}

	float MeasureTextHeight(const std::string& text, float size)
	{
		if (text.empty())
			return 0.0f;

		stb_easy_font_spacing(0.0f);
		std::string mutableText = text;
		const float height = static_cast<float>(stb_easy_font_height(mutableText.data()));
		return height * (size / kBuiltInLineHeight);
	}

}  // namespace Forge
