#include "fgpch.h"
#include "Forge/Scene/SceneSerializer.h"

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"

#include <fstream>

#include <yaml-cpp/yaml.h>

namespace YAML {

	template<>
	struct convert<glm::vec2>
	{
		static Node encode(const glm::vec2& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec2& rhs)
		{
			if (!node.IsSequence() || node.size() != 2)
				return false;

			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			return true;
		}
	};

	template<>
	struct convert<glm::vec3>
	{
		static Node encode(const glm::vec3& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec3& rhs)
		{
			if (!node.IsSequence() || node.size() != 3)
				return false;

			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			rhs.z = node[2].as<float>();
			return true;
		}
	};

	template<>
	struct convert<glm::vec4>
	{
		static Node encode(const glm::vec4& rhs)
		{
			Node node;
			node.push_back(rhs.x);
			node.push_back(rhs.y);
			node.push_back(rhs.z);
			node.push_back(rhs.w);
			node.SetStyle(EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec4& rhs)
		{
			if (!node.IsSequence() || node.size() != 4)
				return false;

			rhs.x = node[0].as<float>();
			rhs.y = node[1].as<float>();
			rhs.z = node[2].as<float>();
			rhs.w = node[3].as<float>();
			return true;
		}
	};

	template<>
	struct convert<Forge::UUID>
	{
		static Node encode(const Forge::UUID& uuid)
		{
			Node node;
			// Encode as string to avoid scientific notation for large uint64_t values
			node.push_back(std::to_string((uint64_t)uuid));
			node.SetStyle(YAML::EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, Forge::UUID& uuid)
		{
			try
			{
				uuid = Forge::UUID((uint64_t)node.as<uint64_t>());
				return true;
			}
			catch (...)
			{
				// Fallback: parse as string (scientific notation guard)
				std::string str = node.as<std::string>();
				uuid = Forge::UUID((uint64_t)std::stoull(str));
				return true;
			}
		}
	};

}

namespace Forge {

	SceneSerializer::SceneSerializer(const Ref<Scene>& scene)
		: m_Scene(scene)
	{
	}

	static void SerializeEntity(YAML::Emitter& out, Entity entity)
	{
		FG_CORE_ASSERT_MSG(entity.HasComponent<IDComponent>(), "Assertion failed!");

		out << YAML::BeginMap; // Entity
		out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

		if (entity.HasComponent<TagComponent>())
		{
			out << YAML::Key << "TagComponent";
			out << YAML::BeginMap;

			auto& tag = entity.GetComponent<TagComponent>().Tag;
			out << YAML::Key << "Tag" << YAML::Value << tag;

			out << YAML::EndMap;
		}

		if (entity.HasComponent<TransformComponent>())
		{
			out << YAML::Key << "TransformComponent";
			out << YAML::BeginMap;

			auto& tc = entity.GetComponent<TransformComponent>();

			// Explicitly build YAML node for vec3 (convert template not reliable for glm types)
			YAML::Node translationNode;
			translationNode.push_back(tc.Translation.x);
			translationNode.push_back(tc.Translation.y);
			translationNode.push_back(tc.Translation.z);
			translationNode.SetStyle(YAML::EmitterStyle::Flow);
			out << YAML::Key << "Translation" << YAML::Value << translationNode;

			YAML::Node rotationNode;
			rotationNode.push_back(tc.Rotation.x);
			rotationNode.push_back(tc.Rotation.y);
			rotationNode.push_back(tc.Rotation.z);
			rotationNode.SetStyle(YAML::EmitterStyle::Flow);
			out << YAML::Key << "Rotation" << YAML::Value << rotationNode;

			YAML::Node scaleNode;
			scaleNode.push_back(tc.Scale.x);
			scaleNode.push_back(tc.Scale.y);
			scaleNode.push_back(tc.Scale.z);
			scaleNode.SetStyle(YAML::EmitterStyle::Flow);
			out << YAML::Key << "Scale" << YAML::Value << scaleNode;

			out << YAML::EndMap;
		}

		if (entity.HasComponent<CameraComponent>())
		{
			out << YAML::Key << "CameraComponent";
			out << YAML::BeginMap;

			auto& cameraComponent = entity.GetComponent<CameraComponent>();
			out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;

			out << YAML::EndMap;
		}

		if (entity.HasComponent<ScriptComponent>())
		{
			auto& scriptComponent = entity.GetComponent<ScriptComponent>();

			out << YAML::Key << "ScriptComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "ClassName" << YAML::Value << scriptComponent.ClassName;

			out << YAML::EndMap;
		}

		if (entity.HasComponent<RigidbodyComponent>())
		{
			out << YAML::Key << "RigidbodyComponent";
			out << YAML::BeginMap;

			auto& rb = entity.GetComponent<RigidbodyComponent>();
			out << YAML::Key << "BodyType" << YAML::Value << (int)rb.Type;
			out << YAML::Key << "FixedRotation" << YAML::Value << rb.FixedRotation;

			out << YAML::EndMap;
		}

		if (entity.HasComponent<BoxColliderComponent>())
		{
			out << YAML::Key << "BoxColliderComponent";
			out << YAML::BeginMap;

			auto& bc = entity.GetComponent<BoxColliderComponent>();

			// Explicitly build YAML node for vec3
			YAML::Node offsetNode;
			offsetNode.push_back(bc.Offset.x);
			offsetNode.push_back(bc.Offset.y);
			offsetNode.push_back(bc.Offset.z);
			offsetNode.SetStyle(YAML::EmitterStyle::Flow);
			out << YAML::Key << "Offset" << YAML::Value << offsetNode;

			YAML::Node sizeNode;
			sizeNode.push_back(bc.Size.x);
			sizeNode.push_back(bc.Size.y);
			sizeNode.push_back(bc.Size.z);
			sizeNode.SetStyle(YAML::EmitterStyle::Flow);
			out << YAML::Key << "Size" << YAML::Value << sizeNode;

			out << YAML::Key << "Density" << YAML::Value << bc.Density;
			out << YAML::Key << "Friction" << YAML::Value << bc.Friction;
			out << YAML::Key << "Restitution" << YAML::Value << bc.Restitution;

			out << YAML::EndMap;
		}

		out << YAML::EndMap;
	}

	void SceneSerializer::Serialize(const std::string& filepath)
	{
		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << "Untitled";
		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;
		for (auto entityID : m_Scene->m_Registry.view<IDComponent>())
		{
			Entity entity{ entityID, m_Scene.get() };
			if (!entity)
				continue;

			SerializeEntity(out, entity);
		}
		out << YAML::EndSeq;
		out << YAML::EndMap;

		std::ofstream fout(filepath);
		fout << out.c_str();
	}

	void SceneSerializer::SerializeRuntime(const std::string& filepath)
	{
		(void)filepath;
		FG_CORE_ASSERT_MSG(false, "SerializeRuntime not implemented");
	}

	bool SceneSerializer::Deserialize(const std::string& filepath)
	{
		YAML::Node data;
		try
		{
			data = YAML::LoadFile(filepath);
		}
		catch (const YAML::ParserException& e)
		{
			FG_CORE_ERROR("Failed to load scene file '{0}'\n     {1}", filepath, e.what());
			return false;
		}

		if (!data["Scene"])
			return false;

		std::string sceneName = data["Scene"].as<std::string>();
		FG_CORE_TRACE("Deserializing scene '{0}'", sceneName);

		auto entities = data["Entities"];
		if (entities)
		{
			for (auto entityNode : entities)
			{
				uint64_t uuid = entityNode["Entity"].as<uint64_t>();

				std::string name;
				auto tagComponent = entityNode["TagComponent"];
				if (tagComponent)
					name = tagComponent["Tag"].as<std::string>();

				FG_CORE_TRACE("Deserialized entity with ID = {0}, name = {1}", uuid, name);

				Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);

				auto transformComponent = entityNode["TransformComponent"];
				if (transformComponent)
				{
					auto& tc = deserializedEntity.GetComponent<TransformComponent>();
					auto transNode = transformComponent["Translation"];
					tc.Translation = glm::vec3(transNode[0].as<float>(), transNode[1].as<float>(), transNode[2].as<float>());
					auto rotNode = transformComponent["Rotation"];
					tc.Rotation = glm::vec3(rotNode[0].as<float>(), rotNode[1].as<float>(), rotNode[2].as<float>());
					auto scaleNode = transformComponent["Scale"];
					tc.Scale = glm::vec3(scaleNode[0].as<float>(), scaleNode[1].as<float>(), scaleNode[2].as<float>());
				}

				auto cameraComponent = entityNode["CameraComponent"];
				if (cameraComponent)
				{
					auto& cc = deserializedEntity.AddComponent<CameraComponent>();
					cc.Primary = cameraComponent["Primary"].as<bool>();
				}

				auto scriptComponent = entityNode["ScriptComponent"];
				if (scriptComponent)
				{
					auto& sc = deserializedEntity.AddComponent<ScriptComponent>();
					sc.ClassName = scriptComponent["ClassName"].as<std::string>();
				}

				auto rigidbodyComponent = entityNode["RigidbodyComponent"];
				if (rigidbodyComponent)
				{
					auto& rb = deserializedEntity.AddComponent<RigidbodyComponent>();
					rb.Type = (RigidbodyComponent::BodyType)rigidbodyComponent["BodyType"].as<int>();
					rb.FixedRotation = rigidbodyComponent["FixedRotation"].as<bool>();
				}

				auto boxColliderComponent = entityNode["BoxColliderComponent"];
				if (boxColliderComponent)
				{
					auto& bc = deserializedEntity.AddComponent<BoxColliderComponent>();
					auto offsetNode = boxColliderComponent["Offset"];
					bc.Offset = glm::vec3(offsetNode[0].as<float>(), offsetNode[1].as<float>(), offsetNode[2].as<float>());
					auto sizeNode = boxColliderComponent["Size"];
					bc.Size = glm::vec3(sizeNode[0].as<float>(), sizeNode[1].as<float>(), sizeNode[2].as<float>());
					bc.Density = boxColliderComponent["Density"].as<float>();
					bc.Friction = boxColliderComponent["Friction"].as<float>();
					bc.Restitution = boxColliderComponent["Restitution"].as<float>();
				}
			}
		}

		return true;
	}

	bool SceneSerializer::DeserializeRuntime(const std::string& filepath)
	{
		(void)filepath;
		FG_CORE_ASSERT_MSG(false, "DeserializeRuntime not implemented");
		return false;
	}

}
