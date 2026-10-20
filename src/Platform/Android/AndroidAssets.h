#pragma once

#include <string>

struct AAssetManager;

namespace Forge {
	namespace Android {

		// Writes the app's assets out of the package, and makes the
		// directory they land in the working directory.
		//
		// The engine reads its files by path - "resources/models/cube.glb" -
		// and a file inside a package is not a file: it is an entry in a
		// zip, with no path the engine can open. So the assets are written
		// out once, on the first run, and the engine runs from there.
		//
		// Returns false when the assets could not be written, and says why:
		// a game with no models is a game that draws nothing, and saying so
		// is better than drawing an empty world.
		[[nodiscard]] bool SyncAssets(AAssetManager* manager, const std::string& targetRoot);

	}  // namespace Android
}  // namespace Forge
