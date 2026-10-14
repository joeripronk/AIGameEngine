#include "fgpch.h"
#include "Forge/Project/GameExporter.h"

#include <filesystem>
#include <fstream>

namespace Forge {

	bool GameExporter::CopyFile(const std::string& from, const std::string& to)
	{
		std::error_code ec;
		if (!std::filesystem::exists(from, ec))
		{
			FG_CORE_ERROR("Cannot copy missing file: {0}", from);
			return false;
		}

		std::filesystem::path toPath(to);
		if (toPath.has_parent_path())
			std::filesystem::create_directories(toPath.parent_path(), ec);

		std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
		if (ec)
		{
			FG_CORE_ERROR("Failed to copy {0} -> {1}: {2}", from, to, ec.message());
			return false;
		}

		return true;
	}

	bool GameExporter::CopyDirectory(const std::string& from, const std::string& to)
	{
		std::error_code ec;
		if (!std::filesystem::exists(from, ec) || !std::filesystem::is_directory(from, ec))
		{
			FG_CORE_ERROR("Cannot copy missing directory: {0}", from);
			return false;
		}

		std::filesystem::create_directories(to, ec);
		for (auto it = std::filesystem::recursive_directory_iterator(from, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec)
				break;

			const auto& entry = *it;
			std::string relative = std::filesystem::relative(entry.path(), from, ec).string();
			if (ec)
				continue;

			std::string dest = to + "/" + relative;
			if (entry.is_directory(ec))
				std::filesystem::create_directories(dest, ec);
			else if (entry.is_regular_file(ec))
				CopyFile(entry.path().string(), dest);
		}

		return true;
	}

	bool GameExporter::Export(const Options& options)
	{
		std::string output = options.OutputDirectory;
		if (!output.empty() && output.back() != '/' && output.back() != '\\')
			output += '/';

		std::error_code ec;
		std::filesystem::create_directories(output, ec);

		// 1. Copy the runtime executable.
		if (!options.RuntimeExecutable.empty())
		{
			std::string exeName = std::filesystem::path(options.RuntimeExecutable).filename().string();
			CopyFile(options.RuntimeExecutable, output + exeName);
		}

		// 2. Copy the scene.
		if (!options.ScenePath.empty())
		{
			std::string sceneName = std::filesystem::path(options.ScenePath).filename().string();
			CopyFile(options.ScenePath, output + sceneName);
		}

		// 3. Copy resources (scripts, prefabs, models, textures, HDRIs).
		if (!options.ResourcesDirectory.empty())
			CopyDirectory(options.ResourcesDirectory, output + "resources");

		FG_CORE_INFO("Exported game '{0}' to {1}", options.GameName, output);
		return true;
	}

}
