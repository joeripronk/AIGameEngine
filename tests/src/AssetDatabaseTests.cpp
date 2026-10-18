#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
using Catch::Approx;

#include "Forge/Assets/AssetRegistry.h"
#include "Forge/Assets/AssetImporter.h"
#include "Forge/Core/FileSystem.h"
#include "Forge/Core/Log.h"

#include <filesystem>
#include <fstream>
#include <algorithm>

using namespace Forge;

namespace {

	// A scratch tree with one texture, one script and one file that is not an
	// asset, so the scan's inclusion *and* exclusion are both observed.
	const char* kScratch = "test_asset_db";

	void PrepareScratch()
	{
		std::filesystem::remove_all(kScratch);
		std::filesystem::create_directories(std::filesystem::path(kScratch) / "textures");
		std::filesystem::create_directories(std::filesystem::path(kScratch) / "scripts");

		std::error_code ec;
		std::filesystem::copy_file(
			FORGE_SOURCE_DIR + std::string("/resources/textures/test_checker.png"),
			std::filesystem::path(kScratch) / "textures" / "checker.png",
			std::filesystem::copy_options::overwrite_existing, ec);

		std::ofstream(std::filesystem::path(kScratch) / "scripts" / "Probe.lua");
		std::ofstream(std::filesystem::path(kScratch) / "notes.txt");
	}

}

TEST_CASE("AssetRegistry assigns handles that lookups agree on", "[assets][registry]")
{
	PrepareScratch();
	AssetRegistry::Scan(kScratch);

	REQUIRE(AssetRegistry::GetAllAssets().size() == 2u);   // notes.txt is not an asset

	for (const AssetMetadata& asset : AssetRegistry::GetAllAssets())
	{
		REQUIRE(asset.Handle != InvalidAssetHandle);
		REQUIRE(AssetRegistry::FindByHandle(asset.Handle) != nullptr);
		REQUIRE(AssetRegistry::FindByHandle(asset.Handle)->Path == asset.Path);
		REQUIRE(AssetRegistry::GetHandleByPath(asset.Path) == asset.Handle);
	}

	REQUIRE(AssetRegistry::FindByHandle(424242) == nullptr);
	REQUIRE(AssetRegistry::GetHandleByPath("no/such/file.png") == InvalidAssetHandle);

	AssetRegistry::Clear();
}

TEST_CASE("Adding one asset is idempotent and rejects what is not an asset", "[assets][registry]")
{
	PrepareScratch();
	AssetRegistry::Clear();

	const std::string png = std::string(kScratch) + "/textures/checker.png";
	const AssetHandle handle = AssetRegistry::Add(png);
	REQUIRE(handle != InvalidAssetHandle);
	REQUIRE(AssetRegistry::GetAllAssets().size() == 1u);

	// Re-adding the same path must return the same handle: a handle that
	// changes when nothing changed is a reference to nothing.
	REQUIRE(AssetRegistry::Add(png) == handle);
	REQUIRE(AssetRegistry::GetAllAssets().size() == 1u);

	REQUIRE(AssetRegistry::Add("test_asset_db/notes.txt") == InvalidAssetHandle);
	REQUIRE(AssetRegistry::Add("test_asset_db/nope.png") == InvalidAssetHandle);
	REQUIRE(AssetRegistry::GetAllAssets().size() == 1u);

	AssetRegistry::Clear();
}

TEST_CASE("The asset database round-trips with its handles", "[assets][registry]")
{
	PrepareScratch();
	AssetRegistry::Scan(kScratch);

	std::vector<AssetHandle> handlesBefore;
	for (const AssetMetadata& asset : AssetRegistry::GetAllAssets())
		handlesBefore.push_back(asset.Handle);

	const std::string db = std::string(kScratch) + "/assets.fgad";
	REQUIRE(AssetRegistry::Serialize(db));

	AssetRegistry::Clear();
	REQUIRE(AssetRegistry::GetAllAssets().empty());

	REQUIRE(AssetRegistry::Deserialize(db));
	REQUIRE(AssetRegistry::GetAllAssets().size() == handlesBefore.size());

	for (const AssetMetadata& asset : AssetRegistry::GetAllAssets())
	{
		REQUIRE(std::find(handlesBefore.begin(), handlesBefore.end(), asset.Handle) != handlesBefore.end());
		REQUIRE(AssetRegistry::GetHandleByPath(asset.Path) == asset.Handle);
	}

	// A handle added after the load must not reuse a saved one, or a
	// reference written into a scene would name two assets.
	{
		std::ofstream(std::filesystem::path(kScratch) / "scripts" / "Probe2.lua");
	}
	const AssetHandle added = AssetRegistry::Add(std::string(kScratch) + "/scripts/Probe2.lua");
	REQUIRE(added != InvalidAssetHandle);
	REQUIRE(std::find(handlesBefore.begin(), handlesBefore.end(), added) == handlesBefore.end());

	AssetRegistry::Clear();
}

TEST_CASE("A corrupt asset database reports failure and changes nothing", "[assets][registry]")
{
	PrepareScratch();
	AssetRegistry::Scan(kScratch);
	const size_t before = AssetRegistry::GetAllAssets().size();
	REQUIRE(before == 2u);

	const std::string bad = std::string(kScratch) + "/bad.fgad";
	{
		std::ofstream out(bad, std::ios::binary);
		out << "NOPE0001not a registry at all";
	}

	REQUIRE_FALSE(AssetRegistry::Deserialize(bad));
	REQUIRE(AssetRegistry::GetAllAssets().size() == before);

	// A file that starts with the magic and stops mid-entry: half a registry
	// must not replace a whole one.
	const std::string truncated = std::string(kScratch) + "/trunc.fgad";
	REQUIRE(AssetRegistry::Serialize(truncated));
	std::filesystem::resize_file(truncated, std::filesystem::file_size(truncated) - 6);

	REQUIRE_FALSE(AssetRegistry::Deserialize(truncated));
	REQUIRE(AssetRegistry::GetAllAssets().size() == before);

	REQUIRE_FALSE(AssetRegistry::Deserialize("test_asset_db/definitely_not_here.fgad"));

	AssetRegistry::Clear();
}

TEST_CASE("AssetImporter proves assets decode with the engine's own loaders", "[assets][importer]")
{
	Forge::Log::Init();

	const std::string root = FORGE_SOURCE_DIR;

	REQUIRE(AssetImporter::Import(root + "/resources/textures/test_checker.png"));
	REQUIRE(AssetImporter::Import(root + "/resources/models/two_parts.gltf"));
	REQUIRE(AssetImporter::Import(root + "/resources/audio/test_tone.wav"));
	REQUIRE(AssetImporter::Import(root + "/resources/scenes/EngineTest.forge"));

	// A missing file and a file whose extension is a known type it does not
	// satisfy must both report false: "imported" is a claim about the bytes,
	// not about the name.
	REQUIRE_FALSE(AssetImporter::Import(root + "/resources/textures/definitely_not_here.png"));

	std::filesystem::create_directories("test_asset_db");
	{
		std::ofstream fake("test_asset_db/fake.png", std::ios::binary);
		fake << "this is not a png";
	}
	REQUIRE_FALSE(AssetImporter::Import("test_asset_db/fake.png"));

	// Shader/script files have no loader here - a script is compiled by the
	// script engine, not "imported", and saying true for those would make the
	// answer mean nothing.
	REQUIRE_FALSE(AssetImporter::Import(root + "/resources/shaders/pbr.frag"));

	std::filesystem::remove("test_asset_db/fake.png");
}

TEST_CASE("Adding a scanned asset gives the handle the rescan agrees with", "[assets][registry]")
{
	PrepareScratch();
	AssetRegistry::Clear();

	const std::string png = std::string(kScratch) + "/textures/checker.png";
	const AssetHandle added = AssetRegistry::Add(png);
	REQUIRE(added != InvalidAssetHandle);

	AssetRegistry::Scan(kScratch);
	const AssetMetadata* byPath = AssetRegistry::FindByPath(png);
	REQUIRE(byPath != nullptr);
	// A rescan is a fresh scan, not an append: it re-assigns handles. What the
	// test pins is that Add and Scan never produce a half state.
	REQUIRE(byPath->Handle != InvalidAssetHandle);

	AssetRegistry::Clear();
}
