#pragma once

#include "Forge/Core/Base.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace Forge {

	// Classifies project assets by file extension.
	enum class AssetType
	{
		Unknown,
		Scene,       // .forge
		Prefab,      // .forge (in prefabs dir)
		Script,      // .lua
		Model,       // .gltf/.glb/.obj/.fbx
		Texture,     // .png/.jpg/.tga/.bmp
		HDR,         // .hdr
		Shader,      // .glsl/.hlsl/.spv
		Audio,       // .wav/.mp3/.ogg
		Mesh         // alias for Model
	};

	AssetType ClassifyAsset(const std::filesystem::path& path);
	const char* AssetTypeToString(AssetType type);

	// A single asset known to the registry.
	struct AssetMetadata
	{
		std::string Path;        // absolute or project-relative path
		std::string Name;        // filename without extension
		std::string Extension;   // lowercase extension including dot
		AssetType Type = AssetType::Unknown;
		uint64_t FileSize = 0;
	};

	// Scans a directory tree and indexes every supported asset. The registry is
	// window-independent so it backs both the ImGui asset browser and headless
	// AI tooling.
	class AssetRegistry
	{
	public:
		static void Scan(const std::string& rootDirectory);
		static void Clear() { s_Assets.clear(); s_Root.clear(); }

		static const std::vector<AssetMetadata>& GetAllAssets() { return s_Assets; }
		static const std::vector<AssetMetadata>& GetAssetsOfType(AssetType type);
		static const AssetMetadata* FindByPath(const std::string& path);
		static std::string GetRoot() { return s_Root; }

	private:
		static void ScanRecursive(const std::filesystem::path& directory);

		static std::string s_Root;
		static std::vector<AssetMetadata> s_Assets;
	};

}
