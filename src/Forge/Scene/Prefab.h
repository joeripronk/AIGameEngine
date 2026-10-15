#pragma once

#include "Forge/Core/Base.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

namespace Forge {

	class Scene;
	class Entity;

	// A prefab is a reusable, serialized entity template. Instantiating a
	// prefab creates a fresh copy of the entity (and all of its components) in
	// a target scene with a brand-new identity.
	class Prefab
	{
	public:
		Prefab() = default;
		explicit Prefab(const std::string& filepath);

		// Serializes `entity` (and its components) to a prefab file.
		[[nodiscard]] static bool CreateFromEntity(Entity entity, const std::string& filepath);

		// Instantiates this prefab into `scene`. When `usePosition` is true the
		// spawned entity's translation is overridden with `position`.
		Entity Instantiate(Scene* scene, bool usePosition = false, const glm::vec3& position = glm::vec3(0.0f)) const;

		[[nodiscard]] bool IsValid() const;
		const std::string& GetFilePath() const { return m_FilePath; }

	private:
		std::string m_FilePath;

		// Whether the file exists and actually parses as an entity file. Computed
		// once in the constructor, because parsing on every IsValid() call would
		// make a hot lookup read and parse a file each time.
		bool m_IsValid = false;
	};

	// Loads and caches prefabs by name from the project's prefab directory.
	class PrefabLibrary
	{
	public:
		// Returns a cached prefab, loading it from `directory/<name>.forge` on
		// first use. Returns nullptr when the file cannot be found.
		[[nodiscard]] static Ref<Prefab> Get(const std::string& name, const std::string& directory = "");

		static void Clear() { s_Prefabs.clear(); }
		static void SetDirectory(const std::string& directory) { s_Directory = directory; }
		static const std::string& GetDirectory() { return s_Directory; }

	private:
		static std::unordered_map<std::string, Ref<Prefab>> s_Prefabs;
		static std::string s_Directory;
	};

}
