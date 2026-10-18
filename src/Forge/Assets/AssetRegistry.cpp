#include "fgpch.h"
#include "Forge/Assets/AssetRegistry.h"

#include <algorithm>
#include <fstream>

namespace Forge {

	std::string AssetRegistry::s_Root;
	std::vector<AssetMetadata> AssetRegistry::s_Assets;
	std::unordered_map<AssetHandle, size_t> AssetRegistry::s_AssetsByHandle;
	AssetHandle AssetRegistry::s_NextHandle = 1;

	namespace {
		// The asset-database file identity, Hazel's ".hzdb" role: a file that
		// claims to be a registry and is not one must be rejected, not
		// half-parsed.
		constexpr char kAssetDatabaseMagic[4] = { 'F', 'G', 'A', 'D' };
		constexpr uint32_t kAssetDatabaseVersion = 1u;
	}

	AssetType ClassifyAsset(const std::filesystem::path& path)
	{
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (ext == ".forge") return AssetType::Scene;
		if (ext == ".lua") return AssetType::Script;
		if (ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx") return AssetType::Model;
		if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp") return AssetType::Texture;
		if (ext == ".hdr") return AssetType::HDR;
		if (ext == ".glsl" || ext == ".hlsl" || ext == ".spv" || ext == ".vert" || ext == ".frag") return AssetType::Shader;
		if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") return AssetType::Audio;

		return AssetType::Unknown;
	}

	const char* AssetTypeToString(AssetType type)
	{
		switch (type)
		{
		case AssetType::Scene: return "Scene";
		case AssetType::Prefab: return "Prefab";
		case AssetType::Script: return "Script";
		case AssetType::Model: return "Model";
		case AssetType::Texture: return "Texture";
		case AssetType::HDR: return "HDR";
		case AssetType::Shader: return "Shader";
		case AssetType::Audio: return "Audio";
		default: return "Unknown";
		}
	}

	void AssetRegistry::Scan(const std::string& rootDirectory)
	{
		s_Assets.clear();
		s_AssetsByHandle.clear();
		s_NextHandle = 1;

		std::string root = rootDirectory;
		if (root.empty())
		{
			const std::filesystem::path assets = Project::GetAssetDirectory();
			root = assets.empty() ? "resources" : assets.string();
		}

		s_Root = root;

		std::error_code ec;
		if (!std::filesystem::exists(root, ec))
		{
			FG_CORE_WARN("AssetRegistry root does not exist: {0}", root);
			return;
		}

		ScanRecursive(root);
		FG_CORE_INFO("AssetRegistry indexed {0} assets under {1}", s_Assets.size(), root);
	}

	void AssetRegistry::ScanRecursive(const std::filesystem::path& directory)
	{
		std::error_code ec;
		for (auto it = std::filesystem::directory_iterator(directory, ec); it != std::filesystem::directory_iterator(); it.increment(ec))
		{
			if (ec)
				break;

			const auto& entry = *it;
			if (entry.is_directory(ec))
			{
				// Skip hidden directories (e.g. .git, build).
				std::string filename = entry.path().filename().string();
				if (!filename.empty() && filename[0] == '.')
					continue;

				ScanRecursive(entry.path());
			}
			else if (entry.is_regular_file(ec))
			{
				AssetMetadata metadata;
				metadata.Path = entry.path().string();
				metadata.Name = entry.path().stem().string();
				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				metadata.Extension = ext;
				metadata.Type = ClassifyAsset(entry.path());

				if (metadata.Type != AssetType::Unknown)
				{
					metadata.FileSize = (uint64_t)entry.file_size(ec);
					RegisterAsset(metadata);
				}
			}
		}
	}

	AssetHandle AssetRegistry::RegisterAsset(AssetMetadata metadata)
	{
		metadata.Handle = s_NextHandle++;
		const size_t index = s_Assets.size();
		s_Assets.push_back(metadata);
		s_AssetsByHandle[metadata.Handle] = index;
		return metadata.Handle;
	}

	AssetHandle AssetRegistry::Add(const std::string& path)
	{
		if (const AssetHandle existing = GetHandleByPath(path); existing != InvalidAssetHandle)
			return existing;

		std::error_code ec;
		if (!std::filesystem::is_regular_file(path, ec))
		{
			FG_CORE_WARN("AssetRegistry: cannot add missing file: {0}", path);
			return InvalidAssetHandle;
		}

		AssetMetadata metadata;
		metadata.Path = path;
		metadata.Name = std::filesystem::path(path).stem().string();
		std::string ext = std::filesystem::path(path).extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		metadata.Extension = ext;
		metadata.Type = ClassifyAsset(path);
		metadata.FileSize = (uint64_t)std::filesystem::file_size(path, ec);

		if (metadata.Type == AssetType::Unknown)
		{
			FG_CORE_WARN("AssetRegistry: not a supported asset type: {0}", path);
			return InvalidAssetHandle;
		}

		return RegisterAsset(metadata);
	}

	AssetHandle AssetRegistry::GetHandleByPath(const std::string& path)
	{
		if (const AssetMetadata* found = FindByPath(path))
			return found->Handle;
		return InvalidAssetHandle;
	}

	const AssetMetadata* AssetRegistry::FindByHandle(AssetHandle handle)
	{
		const auto it = s_AssetsByHandle.find(handle);
		if (it == s_AssetsByHandle.end())
			return nullptr;
		return &s_Assets[it->second];
	}

	bool AssetRegistry::Serialize(const std::string& filepath)
	{
		std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			FG_CORE_ERROR("AssetRegistry: cannot open for writing: {0}", filepath);
			return false;
		}

		const uint32_t version = kAssetDatabaseVersion;
		const uint32_t count = (uint32_t)s_Assets.size();
		out.write(kAssetDatabaseMagic, 4);
		out.write((const char*)&version, sizeof(version));
		out.write((const char*)&count, sizeof(count));

		for (const AssetMetadata& asset : s_Assets)
		{
			const uint32_t pathLength = (uint32_t)asset.Path.size();
			out.write((const char*)&asset.Handle, sizeof(asset.Handle));
			out.write((const char*)&asset.Type, sizeof(asset.Type));
			out.write((const char*)&asset.FileSize, sizeof(asset.FileSize));
			out.write((const char*)&pathLength, sizeof(pathLength));
			out.write(asset.Path.data(), pathLength);
		}

		out.flush();
		if (!out)
		{
			FG_CORE_ERROR("AssetRegistry: failed writing {0}", filepath);
			return false;
		}
		return true;
	}

	bool AssetRegistry::Deserialize(const std::string& filepath)
	{
		std::ifstream in(filepath, std::ios::binary);
		if (!in)
		{
			FG_CORE_WARN("AssetRegistry: cannot open: {0}", filepath);
			return false;
		}

		char magic[4];
		uint32_t version = 0, count = 0;
		in.read(magic, 4);
		in.read((char*)&version, sizeof(version));
		in.read((char*)&count, sizeof(count));
		if (!in || !std::equal(magic, magic + 4, kAssetDatabaseMagic) || version != kAssetDatabaseVersion)
		{
			FG_CORE_WARN("AssetRegistry: not an asset database (or version mismatch): {0}", filepath);
			return false;
		}

		// Parse into a temporary registry and swap only if every entry made it,
		// so a corrupt file cannot leave the engine with a half-known world.
		std::vector<AssetMetadata> assets;
		std::unordered_map<AssetHandle, size_t> byHandle;
		AssetHandle nextHandle = 1;

		for (uint32_t i = 0; i < count && in; ++i)
		{
			AssetMetadata metadata;
			uint32_t pathLength = 0;
			in.read((char*)&metadata.Handle, sizeof(metadata.Handle));
			in.read((char*)&metadata.Type, sizeof(metadata.Type));
			in.read((char*)&metadata.FileSize, sizeof(metadata.FileSize));
			in.read((char*)&pathLength, sizeof(pathLength));
			if (!in || pathLength > (1u << 22))
				break;

			metadata.Path.resize(pathLength);
			in.read(metadata.Path.data(), pathLength);
			if (!in)
				break;

			metadata.Name = std::filesystem::path(metadata.Path).stem().string();
			std::string ext = std::filesystem::path(metadata.Path).extension().string();
			std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
			metadata.Extension = ext;

			byHandle[metadata.Handle] = assets.size();
			assets.push_back(std::move(metadata));
		}

		if (!in)
		{
			FG_CORE_WARN("AssetRegistry: truncated asset database, registry unchanged: {0}", filepath);
			return false;
		}

		for (const AssetMetadata& asset : assets)
			nextHandle = std::max(nextHandle, asset.Handle + 1);

		s_Assets = std::move(assets);
		s_AssetsByHandle = std::move(byHandle);
		s_NextHandle = nextHandle;

		FG_CORE_INFO("AssetRegistry: loaded {0} assets from {1}", s_Assets.size(), filepath);
		return true;
	}

	const std::vector<AssetMetadata>& AssetRegistry::GetAssetsOfType(AssetType type)
	{
		static std::vector<AssetMetadata> s_Filter;
		s_Filter.clear();
		for (const auto& asset : s_Assets)
			if (asset.Type == type)
				s_Filter.push_back(asset);
		return s_Filter;
	}

	const AssetMetadata* AssetRegistry::FindByPath(const std::string& path)
	{
		for (const auto& asset : s_Assets)
			if (asset.Path == path)
				return &asset;
		return nullptr;
	}

}
