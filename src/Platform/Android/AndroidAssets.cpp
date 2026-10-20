#include "fgpch.h"

#include "Platform/Android/AndroidAssets.h"

#include "Forge/Core/Log.h"

#include <android/asset_manager.h>

#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace {

	// The directory the assets are copied from: the package's own assets
	// directory, which the Gradle build fills with the engine's resources.
	constexpr const char* kAssetRoot = "";

	// Copies one asset into the target directory.
	bool CopyAsset(AAssetManager* manager, const std::string& name, const std::string& targetRoot)
	{
		AAsset* asset = AAssetManager_open(manager, name.c_str(), AASSET_MODE_STREAMING);
		if (!asset)
			return false;

		const off64_t length = AAsset_getLength64(asset);
		const std::filesystem::path path = std::filesystem::path(targetRoot) / name;

		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);

		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			AAsset_close(asset);
			FG_CORE_ERROR("Android: could not write '{0}'", path.string());
			return false;
		}

		// The asset is read in chunks: a model can be tens of megabytes,
		// and a buffer that size on a phone is a buffer that may not be
		// there.
		std::vector<char> buffer(64 * 1024);
		bool ok = true;
		while (true)
		{
			const int read = AAsset_read(asset, buffer.data(), buffer.size());
			if (read <= 0)
				break;

			out.write(buffer.data(), read);
			if (!out)
			{
				ok = false;
				break;
			}
		}

		AAsset_close(asset);

		if (!ok)
		{
			FG_CORE_ERROR("Android: could not write '{0}'", path.string());
			return false;
		}

		return true;
	}

	// Copies everything under a directory of the package.
	bool CopyDirectory(AAssetManager* manager, const std::string& name, const std::string& targetRoot)
	{
		AAssetDir* dir = AAssetManager_openDir(manager, name.c_str());
		if (!dir)
			return false;

		bool ok = true;
		while (true)
		{
			const char* entry = AAssetDir_getNextFileName(dir);
			if (!entry || *entry == '\0')
				break;

			const std::string child = name.empty() ? std::string(entry) : name + "/" + entry;

			// A directory is a name with no extension in the package's
			// listing; a file is anything else. (The listing does not say
			// which is which, and the engine's assets are all files.)
			AAssetDir* subdir = AAssetManager_openDir(manager, child.c_str());
			if (subdir)
			{
				AAssetDir_close(subdir);
				ok = CopyDirectory(manager, child, targetRoot) && ok;
			}
			else
			{
				ok = CopyAsset(manager, child, targetRoot) && ok;
			}
		}

		AAssetDir_close(dir);
		return ok;
	}

}  // namespace

namespace Forge {
	namespace Android {

		bool SyncAssets(AAssetManager* manager, const std::string& targetRoot)
		{
			if (!manager)
			{
				FG_CORE_ERROR("Android: no asset manager; the game has no assets");
				return false;
			}

			std::error_code error;
			std::filesystem::create_directories(targetRoot, error);
			if (error)
			{
				FG_CORE_ERROR("Android: could not create '{0}': {1}", targetRoot, error.message());
				return false;
			}

			if (!CopyDirectory(manager, kAssetRoot, targetRoot))
			{
				FG_CORE_ERROR("Android: the assets could not be written to '{0}'", targetRoot);
				return false;
			}

			// The engine reads its files by path, and the paths are
			// relative: the working directory is where they are resolved
			// from.
			if (chdir(targetRoot.c_str()) != 0)
			{
				FG_CORE_ERROR("Android: could not enter '{0}'", targetRoot);
				return false;
			}

			FG_CORE_INFO("Android: assets in '{0}'", targetRoot);
			return true;
		}

	}  // namespace Android
}  // namespace Forge
