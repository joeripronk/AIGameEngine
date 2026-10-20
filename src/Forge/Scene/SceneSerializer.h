#pragma once

#include "Forge/Core/Base.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Forge {

	class Scene;
	class Entity;

	class SceneSerializer
	{
	public:
		SceneSerializer(const Ref<Scene>& scene);

		// Writes the scene to `filepath`. Returns false if the file could not be
		// opened or written; callers must not treat a failed save as success.
		[[nodiscard]] bool Serialize(const std::string& filepath);

		void SerializeRuntime(const std::string& filepath);

		[[nodiscard]] bool Deserialize(const std::string& filepath);
		[[nodiscard]] bool DeserializeRuntime(const std::string& filepath);

		// Entity-level (de)serialization, used by the prefab system. These
		// operate on a single entity's components and never touch the scene's
		// entity map of the source scene.
		[[nodiscard]] static bool SerializeEntityToFile(Entity entity, const std::string& filepath);
		static Entity DeserializeEntityFromFile(Scene* scene, const std::string& filepath,
			bool usePositionOverride = false, const glm::vec3& positionOverride = glm::vec3(0.0f));

		// Whether `filepath` parses as a serialized entity file. Used to reject a
		// corrupted or unrelated file up front instead of failing silently at
		// instantiation time.
		[[nodiscard]] static bool IsEntityFile(const std::string& filepath);

		// The same entity, as text rather than as a file. The editor's undo
		// stack keeps the state of an entity before and after an edit, and a
		// file per edit is a file per edit on disk.
		//
		// The text holds the entity's identity as well as its components, so
		// restoring it brings the same entity back rather than a copy of it.
		[[nodiscard]] static std::string SerializeEntityToString(Entity entity);

		// The components a state mentions. The undo stack needs them: a
		// state that mentions fewer components than the entity has is a
		// state that cannot be written onto the entity as it is, because a
		// component can be replaced but not taken away in place.
		[[nodiscard]] static std::vector<std::string> GetStateComponents(const std::string& state);

		// Writes a state onto an entity that is already there: the values
		// the state carries are set, and the entity keeps its identity in
		// every sense - the same handle, the same place in the hierarchy.
		//
		// False when the state cannot be written onto the entity - the state
		// is not an entity's, or the entity has components the state does
		// not mention.
		[[nodiscard]] static bool ApplyEntityState(Entity entity, const std::string& state);

		// Restores an entity from the text SerializeEntityToString wrote. The
		// entity is created with the identity the text carries, so an undone
		// edit restores the entity that was edited and not a new one.
		//
		// A null Entity when the text is not an entity.
		[[nodiscard]] static Entity DeserializeEntityFromString(Scene* scene, const std::string& text);

	private:
		Ref<Scene> m_Scene;
	};

}
