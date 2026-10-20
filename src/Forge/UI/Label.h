#pragma once

#include "Forge/Core/Base.h"

#include <glm/glm.hpp>
#include <string>

namespace Forge {

	class CpuMesh;

	// A piece of text in the world.
	//
	// The label's colour is the entity's material, so a label is coloured
	// the same way a cube is.
	//
	// There are two fonts. A label with a typeface draws from it: the
	// glyphs are pixels with a coverage, so the text is textured and it is
	// blended. A label with no typeface draws from the engine's built-in
	// vector font: the glyphs are solid shapes, so the text needs no
	// texture and no blending and is drawn by the same path as any other
	// mesh.
	struct LabelComponent
	{
		// The text to show.
		std::string Text;

		// The typeface. Loaded from FontPath when it is set and the font is
		// not loaded yet.
		Ref<class Font> Font;

		// The typeface to load. Empty means the engine's built-in one.
		std::string FontPath;

		// How tall the text is, in world units.
		float Size = 0.5f;

		// Whether the label faces the camera. A label that does not is
		// placed in the world like any other object.
		bool Billboard = true;

		// The mesh the text was built into. The label rebuilds it when the
		// text, the size or the typeface changes.
		Ref<class CpuMesh> MeshData;

		// What the mesh was built from, so a rebuild happens when (and only
		// when) one of them changes.
		std::string BuiltText;
		float BuiltSize = 0.0f;
		std::string BuiltFontPath;

		// Whether the text is drawn from a typeface - and so is textured and
		// blended - or from the built-in font.
		[[nodiscard]] bool IsTextured() const { return Font != nullptr; }
	};

	// Builds the mesh of a text from the engine's built-in vector font: the
	// glyphs as quads, facing +Z.
	//
	// The text is drawn in the font's own pixel space, with the origin at
	// the left of the first line and the baseline below it, and scaled so
	// that the text is `size` world units tall.
	//
	// A null mesh when the text is empty: there is nothing to draw, and a
	// mesh with no vertices is a draw that does nothing.
	[[nodiscard]] Ref<CpuMesh> BuildTextMesh(const std::string& text, float size);

	// Builds the mesh of a text from a typeface: one quad per glyph, with
	// the atlas's coordinates.
	//
	// A null mesh when the text is empty or the typeface has no glyphs.
	[[nodiscard]] Ref<CpuMesh> BuildFontTextMesh(const Ref<class Font>& font, const std::string& text, float size);

	// The width of a text in world units - what the label's own size makes
	// it. Zero for an empty text.
	[[nodiscard]] float MeasureTextWidth(const std::string& text, float size);

	// The height of a text in world units, the same way.
	[[nodiscard]] float MeasureTextHeight(const std::string& text, float size);

}  // namespace Forge
