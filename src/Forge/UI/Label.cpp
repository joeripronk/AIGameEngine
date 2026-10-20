#include "fgpch.h"
#include "Forge/UI/Label.h"

#include "Forge/Renderer/CpuMesh.h"

// The engine's font: stb_easy_font's vector font, which is in the public
// domain and needs no asset. The glyphs are quads, so a label is a mesh -
// no texture, no blending, and the same draw path as any other geometry.
#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"

namespace Forge {

	namespace {

		// The font's quads are in a pixel space with the y axis pointing
		// down. The label's are in world units with the y axis pointing up,
		// so the text is not upside down.
		constexpr float kPixelsPerUnit = 1.0f;

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

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		vertices.reserve(static_cast<size_t>(quadCount) * 4);
		indices.reserve(static_cast<size_t>(quadCount) * 6);

		const float scale = size * kPixelsPerUnit;

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

	float MeasureTextHeight(const std::string& text, float size)
	{
		if (text.empty())
			return 0.0f;

		stb_easy_font_spacing(0.0f);
		std::string mutableText = text;
		const float height = static_cast<float>(stb_easy_font_height(mutableText.data()));
		return height * size * kPixelsPerUnit;
	}

	float MeasureTextWidth(const std::string& text, float size)
	{
		if (text.empty())
			return 0.0f;

		stb_easy_font_spacing(0.0f);
		std::string mutableText = text;
		const float width = static_cast<float>(stb_easy_font_width(mutableText.data()));
		return width * size * kPixelsPerUnit;
	}

}  // namespace Forge
