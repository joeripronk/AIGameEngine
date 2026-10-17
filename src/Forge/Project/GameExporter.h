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

			// An empty ScenePath or ResourcesDirectory means "what the active
			// project says" - its start scene, its asset directory. They were
			// previously hardcoded defaults ("resources/"), which meant the
			// exporter ignored the project and shipped whatever the engine
			// happened to have in the working directory. With no project open,
			// resources fall back to `resources/` and an empty scene means the
			// caller genuinely wants a package without one.
			std::string ScenePath;
			std::string ResourcesDirectory;

			std::string RuntimeExecutable;         // path to ForgeRuntime binary
			std::string GameName = "Game";
		};

		[[nodiscard]] static bool Export(const Options& options);

	private:
		static bool CopyFile(const std::string& from, const std::string& to);
		static bool CopyDirectory(const std::string& from, const std::string& to);
	};

}
