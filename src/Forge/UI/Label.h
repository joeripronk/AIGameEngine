#pragma once

#include "Forge/Core/Base.h"

#include <glm/glm.hpp>
#include <string>

namespace Forge {

	class CpuMesh;

	// A piece of text in the world.
	//
	// The engine's font is a built-in vector font: the glyphs are quads, so
	// the text needs no texture and no blending - a label is drawn by the
	// same path as any other mesh, and a label is the one thing a game
	// cannot do without.
	//
	// The label's colour is the entity's material, so a label is coloured
	// the same way a cube is.
	struct LabelComponent
	{
		// The text to show.
		std::string Text;

		// The world units per pixel of the font. The font is drawn in
		// pixels and the label is this many world units tall per pixel.
		float Size = 0.01f;

		// Whether the label faces the camera. A label that does not is
		// placed in the world like any other object.
		bool Billboard = true;

		// The mesh the text was built into. The label rebuilds it when the
		// text or the size changes.
		Ref<class CpuMesh> MeshData;

		// The text the mesh was built from, so a rebuild happens when (and
		// only when) the text changes.
		std::string BuiltText;
		float BuiltSize = 0.0f;
	};

	// Builds the mesh of a text: the glyphs as quads, facing +Z.
	//
	// The text is drawn in the font's own pixel space, with the origin at
	// the left of the first line and the baseline below it, and scaled by
	// `size` (the world units per pixel).
	//
	// A null mesh when the text is empty: there is nothing to draw, and a
	// mesh with no vertices is a draw that does nothing.
	[[nodiscard]] Ref<CpuMesh> BuildTextMesh(const std::string& text, float size);

	// The width of a text in world units - what the label's own size makes
	// it. Zero for an empty text.
	[[nodiscard]] float MeasureTextWidth(const std::string& text, float size);

	// The height of a text in world units, the same way.
	[[nodiscard]] float MeasureTextHeight(const std::string& text, float size);

}  // namespace Forge
