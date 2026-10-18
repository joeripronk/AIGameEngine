#pragma once

#include <string>

namespace Forge {

	// Importing an asset means proving it decodes with the engine's own
	// loaders - Hazel's per-type importer dispatch (AssetImporter →
	// TextureImporter et al.), adapted to the loaders Forge already has:
	// CpuTexture, CpuMesh, AudioEngine and SceneSerializer. "Imported" is
	// therefore decidable, not asserted: an asset that reports true is one
	// the engine can actually draw, play or open.
	//
	// This is the single answer an AI agent gets to "is this file usable?",
	// which each of those loaders answers separately today.
	namespace AssetImporter {

		// Runs the loader for the asset's type. Returns false for a missing
		// file, a type with no loader (shader, script, unknown), and for a
		// file that names a known type but does not decode. Every failure is
		// logged by the loader that hit it.
		[[nodiscard]]
		bool Import(const std::string& path);

	}  // namespace AssetImporter

}
