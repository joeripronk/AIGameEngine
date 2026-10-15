#include "fgpch.h"
#include "Forge/Scene/Prefab.h"

#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/SceneSerializer.h"

#include <filesystem>

namespace Forge {

	Prefab::Prefab(const std::string& filepath)
		: m_FilePath(filepath)
		, m_IsValid(!filepath.empty() && std::filesystem::exists(filepath)
			&& SceneSerializer::IsEntityFile(filepath))
	{
		// Existence alone is not validity: a corrupted or unrelated file passes
		// the exists() check and then yields a silent no-op at Instantiate time.
		// Callers check IsValid() precisely to be able to trust Instantiate.
		if (!m_IsValid && !filepath.empty())
			FG_CORE_WARN("Prefab file is missing or unreadable: {0}", filepath);
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
		return m_IsValid;
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
