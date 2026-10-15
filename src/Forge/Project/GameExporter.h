#pragma once

#include "Forge/Core/Base.h"

#include <string>

namespace Forge {

	// Packages a game into a self-contained, distributable folder: the runtime
	// executable, the scene, and all project resources. The result is a game
	// that runs without any editing capability.
	class GameExporter
	{
	public:
		struct Options
		{
			std::string OutputDirectory = "dist/";
			std::string ScenePath;                 // source .forge scene to ship
			std::string ResourcesDirectory = "resources/";
			std::string RuntimeExecutable;         // path to ForgeRuntime binary
			std::string GameName = "Game";
		};

		[[nodiscard]] static bool Export(const Options& options);

	private:
		static bool CopyFile(const std::string& from, const std::string& to);
		static bool CopyDirectory(const std::string& from, const std::string& to);
	};

}
