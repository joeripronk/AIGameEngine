#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Forge/Core/FileSystem.h"
#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/UI/Font.h"
#include "Forge/UI/Label.h"

#include <cstdio>

using namespace Forge;

namespace {

	// The typeface the tests draw with. The engine ships one, so the test
	// does not depend on what the machine has installed.
	const char* kFontPath = "resources/fonts/DejaVuSans.ttf";

}  // namespace

TEST_CASE("A typeface is rasterised into an atlas of glyphs", "[ui][font]")
{
	Ref<Font> font = Font::Load(kFontPath, 32);
	REQUIRE(font);

	// The atlas holds the printable ASCII, white with the coverage in the
	// alpha.
	REQUIRE(font->GetWidth() > 0);
	REQUIRE(font->GetHeight() > 0);
	REQUIRE(font->GetPixels().size() == static_cast<size_t>(font->GetWidth()) * font->GetHeight() * 4);

	// Some glyph is covered: an atlas of nothing is not an atlas.
	bool anyCoverage = false;
	for (size_t i = 3; i < font->GetPixels().size(); i += 4)
	{
		if (font->GetPixels()[i] > 0)
		{
			anyCoverage = true;
			break;
		}
	}
	REQUIRE(anyCoverage);

	// A glyph has a place in the atlas and moves the pen.
	Font::Glyph glyph{};
	REQUIRE(font->GetGlyph('A', glyph));
	REQUIRE(glyph.width > 0.0f);
	REQUIRE(glyph.height > 0.0f);
	REQUIRE(glyph.advance > 0.0f);
	REQUIRE(glyph.u1 > glyph.u0);
	REQUIRE(glyph.v1 > glyph.v0);

	// A character the atlas does not hold has no glyph.
	Font::Glyph missing{};
	REQUIRE_FALSE(font->GetGlyph('\x01', missing));

	// The text is as wide as the glyphs it uses.
	REQUIRE(font->MeasureTextWidth("AA") > font->MeasureTextWidth("A"));
	REQUIRE(font->MeasureTextWidth("") == 0.0f);
}

TEST_CASE("A file that is not a typeface is not a typeface", "[ui][font]")
{
	// A file that is not there.
	REQUIRE(Font::Load("resources/fonts/does_not_exist.ttf") == nullptr);

	// A file that is there and is not a typeface.
	const char* path = "test_not_a_font.ttf";
	{
		FILE* file = std::fopen(path, "wb");
		REQUIRE(file);
		const char* rubbish = "this is not a typeface, it is a line of text";
		std::fwrite(rubbish, 1, std::strlen(rubbish), file);
		std::fclose(file);
	}

	REQUIRE(Font::Load(path) == nullptr);
	std::remove(path);
}

TEST_CASE("A text is built from a typeface, one quad per glyph", "[ui][font][label]")
{
	Ref<Font> font = Font::Load(kFontPath, 32);
	REQUIRE(font);

	Ref<CpuMesh> mesh = BuildFontTextMesh(font, "AB", 0.5f);
	REQUIRE(mesh);

	// Two glyphs, four corners each.
	REQUIRE(mesh->GetVertices().size() == 8);
	REQUIRE(mesh->GetIndexCount() == 12);

	// The quads carry the atlas's coordinates, so the texture can find the
	// glyphs.
	bool hasAtlasCoordinates = false;
	for (const Vertex& vertex : mesh->GetVertices())
	{
		if (vertex.UV.x > 0.0f || vertex.UV.y > 0.0f)
			hasAtlasCoordinates = true;
	}
	REQUIRE(hasAtlasCoordinates);

	// An empty text is nothing to draw.
	REQUIRE(BuildFontTextMesh(font, "", 0.5f) == nullptr);

	// And a text with no typeface is nothing to draw either.
	REQUIRE(BuildFontTextMesh(nullptr, "AB", 0.5f) == nullptr);
}

TEST_CASE("The text is as tall as the label says", "[ui][font][label]")
{
	Ref<Font> font = Font::Load(kFontPath, 32);
	REQUIRE(font);

	// The text is drawn in the font's own pixels and scaled to the size, so
	// the quads' height is the size the label asked for.
	Ref<CpuMesh> small = BuildFontTextMesh(font, "A", 0.25f);
	Ref<CpuMesh> large = BuildFontTextMesh(font, "A", 0.5f);
	REQUIRE(small);
	REQUIRE(large);

	float smallHeight = 0.0f;
	for (const Vertex& vertex : small->GetVertices())
		smallHeight = std::max(smallHeight, vertex.Position.y);

	float largeHeight = 0.0f;
	for (const Vertex& vertex : large->GetVertices())
		largeHeight = std::max(largeHeight, vertex.Position.y);

	// The glyph sits below the baseline, so the height is measured from the
	// bottom: the two differ by the factor the sizes differ by.
	REQUIRE(largeHeight == Catch::Approx(smallHeight * 2.0f).margin(0.001f));
}

TEST_CASE("A label loads its typeface and builds its mesh from it", "[ui][font][label]")
{
	Scene scene;
	Entity entity = scene.CreateEntity("Score");
	entity.AddComponent<MeshComponent>();

	auto& label = entity.AddComponent<LabelComponent>();
	label.Text = "Score: 100";
	label.FontPath = kFontPath;
	label.Size = 0.5f;

	scene.UpdateLabels();

	// The typeface is loaded and the mesh is built from it.
	REQUIRE(label.Font);
	REQUIRE(label.IsTextured());
	REQUIRE(label.MeshData);
	REQUIRE(entity.GetComponent<MeshComponent>().CpuMeshData);

	// And the atlas is the pixels the renderer turns into a texture.
	REQUIRE(label.Font->GetWidth() > 0);
	REQUIRE(label.Font->GetPixels().size() > 0);
}

TEST_CASE("A label with no typeface uses the engine's built-in font", "[ui][font][label]")
{
	Scene scene;
	Entity entity = scene.CreateEntity("Score");
	entity.AddComponent<MeshComponent>();

	auto& label = entity.AddComponent<LabelComponent>();
	label.Text = "Score";
	label.Size = 0.5f;

	scene.UpdateLabels();

	// The built-in font's glyphs are solid shapes, so the text needs no
	// texture and no blending.
	REQUIRE_FALSE(label.IsTextured());
	REQUIRE(label.MeshData);
	REQUIRE(entity.GetComponent<MeshComponent>().CpuMeshData);
}
