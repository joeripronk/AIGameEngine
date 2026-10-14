#include "fgpch.h"
#include "Forge/Scene/Prefab.h"

#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/SceneSerializer.h"

#include <filesystem>

namespace Forge {

	Prefab::Prefab(const std::string& filepath)
		: m_FilePath(filepath)
	{
	}

	bool Prefab::CreateFromEntity(Entity entity, const std::string& filepath)
	{
		// Ensure the destination directory exists.
		std::filesystem::path path(filepath);
		if (path.has_parent_path())
		{
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
		}

		return SceneSerializer::SerializeEntityToFile(entity, filepath);
	}

	Entity Prefab::Instantiate(Scene* scene, bool usePosition, const glm::vec3& position) const
	{
		if (!scene || m_FilePath.empty())
			return {};

		return SceneSerializer::DeserializeEntityFromFile(scene, m_FilePath, usePosition, position);
	}

	bool Prefab::IsValid() const
	{
		return !m_FilePath.empty() && std::filesystem::exists(m_FilePath);
	}

	std::unordered_map<std::string, Ref<Prefab>> PrefabLibrary::s_Prefabs;
	std::string PrefabLibrary::s_Directory = "resources/prefabs/";

	Ref<Prefab> PrefabLibrary::Get(const std::string& name, const std::string& directory)
	{
		// A project may override the search directory at runtime.
		if (!directory.empty())
			s_Directory = directory;

		auto it = s_Prefabs.find(name);
		if (it != s_Prefabs.end())
			return it->second;

		std::string directoryToUse = directory.empty() ? s_Directory : directory;
		std::string filepath = directoryToUse + name + ".forge";

		if (!std::filesystem::exists(filepath))
		{
			FG_CORE_WARN("Prefab '{0}' not found at {1}", name, filepath);
			return nullptr;
		}

		auto prefab = CreateRef<Prefab>(filepath);
		s_Prefabs[name] = prefab;
		return prefab;
	}

}
