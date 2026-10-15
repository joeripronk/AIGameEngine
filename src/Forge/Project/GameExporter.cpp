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
		if (ec)
		{
			FG_CORE_ERROR("Cannot create {0}: {1}", to, ec.message());
			return false;
		}

		// A partially copied resource tree is a broken package, so any failed
		// file aborts the whole directory copy.
		for (auto it = std::filesystem::recursive_directory_iterator(from, ec);
			it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec)
			{
				FG_CORE_ERROR("Failed while walking {0}: {1}", from, ec.message());
				return false;
			}

			const auto& entry = *it;
			const std::string relative = std::filesystem::relative(entry.path(), from, ec).string();
			if (ec)
			{
				FG_CORE_ERROR("Failed to resolve {0}: {1}", entry.path().string(), ec.message());
				return false;
			}

			const std::string dest = to + "/" + relative;
			if (entry.is_directory(ec))
			{
				std::filesystem::create_directories(dest, ec);
				if (ec)
				{
					FG_CORE_ERROR("Cannot create {0}: {1}", dest, ec.message());
					return false;
				}
			}
			else if (entry.is_regular_file(ec))
			{
				if (!CopyFile(entry.path().string(), dest))
					return false;
			}
		}

		return true;
	}

	bool GameExporter::Export(const Options& options)
	{
		std::string output = options.OutputDirectory;
		if (!output.empty() && output.back() != '/' && output.back() != '\\')
			output += '/';

		// Validate every input before writing anything. An export that half
		// succeeds is worse than one that fails loudly: the caller has no way to
		// tell a complete package from a broken one.
		std::error_code ec;

		if (output.empty())
		{
			FG_CORE_ERROR("Export failed: no output directory given");
			return false;
		}

		if (!options.RuntimeExecutable.empty() && !std::filesystem::is_regular_file(options.RuntimeExecutable, ec))
		{
			FG_CORE_ERROR("Export failed: runtime executable not found: {0}", options.RuntimeExecutable);
			return false;
		}

		if (!options.ScenePath.empty() && !std::filesystem::is_regular_file(options.ScenePath, ec))
		{
			FG_CORE_ERROR("Export failed: scene not found: {0}", options.ScenePath);
			return false;
		}

		if (!options.ResourcesDirectory.empty()
			&& !std::filesystem::is_directory(options.ResourcesDirectory, ec))
		{
			FG_CORE_ERROR("Export failed: resources directory not found: {0}", options.ResourcesDirectory);
			return false;
		}

		if (!std::filesystem::create_directories(output, ec) && ec)
		{
			FG_CORE_ERROR("Export failed: cannot create output directory {0}: {1}", output, ec.message());
			return false;
		}

		// 1. Copy the runtime executable.
		if (!options.RuntimeExecutable.empty())
		{
			const std::string exeName = std::filesystem::path(options.RuntimeExecutable).filename().string();
			if (!CopyFile(options.RuntimeExecutable, output + exeName))
				return false;
		}

		// 2. Copy the scene.
		if (!options.ScenePath.empty())
		{
			const std::string sceneName = std::filesystem::path(options.ScenePath).filename().string();
			if (!CopyFile(options.ScenePath, output + sceneName))
				return false;
		}

		// 3. Copy resources (scripts, prefabs, models, textures, HDRIs).
		if (!options.ResourcesDirectory.empty() && !CopyDirectory(options.ResourcesDirectory, output + "resources"))
			return false;

		FG_CORE_INFO("Exported game '{0}' to {1}", options.GameName, output);
		return true;
	}

}
