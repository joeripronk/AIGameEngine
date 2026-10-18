#include "fgpch.h"
#include "Forge/Assets/AssetImporter.h"

#include "Forge/Assets/AssetRegistry.h"
#include "Forge/Audio/AudioEngine.h"
#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Renderer/CpuTexture.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/SceneSerializer.h"

namespace Forge::AssetImporter {

	bool Import(const std::string& path)
	{
		switch (ClassifyAsset(path))
		{
			case AssetType::Texture:
			case AssetType::HDR:
			{
				return CpuTexture::Load(path) != nullptr;
			}
			case AssetType::Model:
			case AssetType::Mesh:
			{
				return CpuMesh::LoadFromFile(path) != nullptr;
			}
			case AssetType::Audio:
			{
				return AudioEngine::LoadAudioBuffer(path) != nullptr;
			}
			case AssetType::Scene:
			case AssetType::Prefab:
			{
				// Opening the scene is the import check: a file that parses is
				// one the engine can run, and one that does not is one the
				// runtime would fail on later, far from here.
				Ref<Scene> scene = CreateRef<Scene>();
				SceneSerializer serializer(scene);
				return serializer.Deserialize(path);
			}
			default:
				FG_CORE_WARN("AssetImporter: no importer for '{0}'", path);
				return false;
		}
	}

}
