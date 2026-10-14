#pragma once

#include "Forge/Core/Base.h"

#include <glm/glm.hpp>

namespace Forge {

	class Scene;
	class Entity;

	class SceneSerializer
	{
	public:
		SceneSerializer(const Ref<Scene>& scene);

		void Serialize(const std::string& filepath);
		void SerializeRuntime(const std::string& filepath);

		bool Deserialize(const std::string& filepath);
		bool DeserializeRuntime(const std::string& filepath);

		// Entity-level (de)serialization, used by the prefab system. These
		// operate on a single entity's components and never touch the scene's
		// entity map of the source scene.
		static bool SerializeEntityToFile(Entity entity, const std::string& filepath);
		static Entity DeserializeEntityFromFile(Scene* scene, const std::string& filepath,
			bool usePositionOverride = false, const glm::vec3& positionOverride = glm::vec3(0.0f));

	private:
		Ref<Scene> m_Scene;
	};

}
