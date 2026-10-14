#include "fgpch.h"
#include "Forge/Assets/AssetRegistry.h"

#include <algorithm>

namespace Forge {

	std::string AssetRegistry::s_Root;
	std::vector<AssetMetadata> AssetRegistry::s_Assets;

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
		s_Root = rootDirectory;

		std::error_code ec;
		if (!std::filesystem::exists(rootDirectory, ec))
		{
			FG_CORE_WARN("AssetRegistry root does not exist: {0}", rootDirectory);
			return;
		}

		ScanRecursive(rootDirectory);
		FG_CORE_INFO("AssetRegistry indexed {0} assets under {1}", s_Assets.size(), rootDirectory);
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
					s_Assets.push_back(metadata);
				}
			}
		}
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
