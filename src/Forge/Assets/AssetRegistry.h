#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Project/Project.h"

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

	// An asset named by a number rather than a string, the way Hazel's asset
	// manager identifies them: a scene, a script and an editor panel can all
	// refer to "the brick texture" without repeating a path that any of them
	// can typo. Handles are assigned by the registry and survive
	// Serialize/Deserialize, which is what makes them referenceable from a
	// file at all.
	using AssetHandle = int64_t;
	inline constexpr AssetHandle InvalidAssetHandle = -1;

	// A single asset known to the registry.
	struct AssetMetadata
	{
		AssetHandle Handle = InvalidAssetHandle;
		std::string Path;        // absolute or project-relative path
		std::string Name;        // filename without extension
		std::string Extension;   // lowercase extension including dot
		AssetType Type = AssetType::Unknown;
		uint64_t FileSize = 0;
	};

	// Scans a directory tree and indexes every supported asset. The registry is
	// window-independent so it backs both the ImGui asset browser and headless
	// AI tooling.
	//
	// An empty root scans the active project's asset directory, falling back
	// to `resources` when no project is open. A caller that knows the root says
	// so; a caller that does not would otherwise have to hardcode the one
	// directory name the engine happens to use.
	class AssetRegistry
	{
	public:
		static void Scan(const std::string& rootDirectory = "");
		static void Clear() { s_Assets.clear(); s_AssetsByHandle.clear(); s_NextHandle = 1; s_Root.clear(); }

		static const std::vector<AssetMetadata>& GetAllAssets() { return s_Assets; }
		static const std::vector<AssetMetadata>& GetAssetsOfType(AssetType type);
		static const AssetMetadata* FindByPath(const std::string& path);
		static std::string GetRoot() { return s_Root; }

		// Registers one file - the ImportAsset entry Hazel's editor uses for
		// assets an agent creates mid-session, which a rescan would pick up
		// only at the next poll. Idempotent: adding a known path returns the
		// handle it already has. Returns InvalidAssetHandle for a missing file
		// or a file that is not a supported asset type.
		[[nodiscard]]
		static AssetHandle Add(const std::string& path);

		static AssetHandle GetHandleByPath(const std::string& path);
		static const AssetMetadata* FindByHandle(AssetHandle handle);

		// The on-disk asset database (Hazel's serialized asset registry): a
		// scan is a fact about a machine's files, while the database is the
		// handles themselves, and handles referenced by anything are only
		// meaningful if they survive. A corrupt file reports false and leaves
		// the registry exactly as it was - half a registry is worse than the
		// one you had.
		[[nodiscard]]
		static bool Serialize(const std::string& filepath);
		[[nodiscard]]
		static bool Deserialize(const std::string& filepath);

	private:
		static void ScanRecursive(const std::filesystem::path& directory);
		static AssetHandle RegisterAsset(AssetMetadata metadata);

		static std::string s_Root;
		static std::vector<AssetMetadata> s_Assets;
		static std::unordered_map<AssetHandle, size_t> s_AssetsByHandle;
		static AssetHandle s_NextHandle;
	};

}
