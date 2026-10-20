#include "fgpch.h"
#include "Forge/Scene/SceneSerializer.h"

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Renderer/CpuMesh.h"

#include "Forge/Core/FileSystem.h"

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

	namespace {

		// yaml-cpp's templated convert<glm::vec3> is not reliably picked up for
		// every nested value, so build nodes explicitly and read them defensively.
		YAML::Node EncodeVec3(const glm::vec3& v)
		{
			YAML::Node node;
			node.push_back(v.x);
			node.push_back(v.y);
			node.push_back(v.z);
			node.SetStyle(YAML::EmitterStyle::Flow);
			return node;
		}

		glm::vec3 DecodeVec3(const YAML::Node& node, const glm::vec3& fallback = glm::vec3(0.0f))
		{
			if (!node || !node.IsSequence() || node.size() != 3)
				return fallback;

			return glm::vec3(node[0].as<float>(), node[1].as<float>(), node[2].as<float>());
		}

		bool HasNode(const YAML::Node& node, const char* key)
		{
			return node && node[key];
		}

		// Scalar accessors that tolerate missing nodes by returning a fallback.
		// This lets older/newer scene files load without throwing.
		float GetFloat(const YAML::Node& node, const char* key, float fallback = 0.0f)
		{
			YAML::Node value = node[key];
			return value ? value.as<float>() : fallback;
		}

		// Whether a node carries anything that identifies it as a serialized
		// entity. Every real entity file has a TransformComponent, because
		// CreateEntity always adds one.
		bool IsEntityNode(const YAML::Node& node)
		{
			static const char* kEntityComponents[] = {
				"TransformComponent", "TagComponent", "CameraComponent",
				"ScriptComponent", "RigidbodyComponent", "BoxColliderComponent",
				"SphereColliderComponent", "LightComponent",
				"PBRMaterialComponent", "MeshComponent", "MultiMeshComponent",
				"LabelComponent",
				"EnvironmentMapComponent", "AudioSourceComponent"
			};

			for (const char* component : kEntityComponents)
			{
				if (HasNode(node, component))
					return true;
			}

			return false;
		}

		bool GetBool(const YAML::Node& node, const char* key, bool fallback = false)
		{
			YAML::Node value = node[key];
			return value ? value.as<bool>() : fallback;
		}

		std::string GetString(const YAML::Node& node, const char* key, const std::string& fallback = std::string())
		{
			YAML::Node value = node[key];
			return value ? value.as<std::string>() : fallback;
		}

		int GetInt(const YAML::Node& node, const char* key, int fallback = 0)
		{
			YAML::Node value = node[key];
			return value ? value.as<int>() : fallback;
		}

	}

	SceneSerializer::SceneSerializer(const Ref<Scene>& scene)
		: m_Scene(scene)
	{
	}

	static void SerializeEntity(YAML::Emitter& out, Entity entity)
	{
		FG_CORE_ASSERT_MSG(entity.HasComponent<IDComponent>(), "Entity has no IDComponent!");

		out << YAML::BeginMap; // Entity
		out << YAML::Key << "Entity" << YAML::Value << entity.GetUUID();

		if (entity.HasComponent<TagComponent>())
		{
			out << YAML::Key << "TagComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Tag" << YAML::Value << entity.GetComponent<TagComponent>().Tag;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<TransformComponent>())
		{
			auto& tc = entity.GetComponent<TransformComponent>();
			out << YAML::Key << "TransformComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Translation" << YAML::Value << EncodeVec3(tc.Translation);
			out << YAML::Key << "Rotation" << YAML::Value << EncodeVec3(tc.Rotation);
			out << YAML::Key << "Scale" << YAML::Value << EncodeVec3(tc.Scale);
			out << YAML::EndMap;
		}

		if (entity.HasComponent<CameraComponent>())
		{
			auto& cameraComponent = entity.GetComponent<CameraComponent>();
			out << YAML::Key << "CameraComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;
			out << YAML::Key << "FieldOfView" << YAML::Value << cameraComponent.FieldOfView;
			out << YAML::Key << "NearClip" << YAML::Value << cameraComponent.NearClip;
			out << YAML::Key << "FarClip" << YAML::Value << cameraComponent.FarClip;
			out << YAML::Key << "AspectRatio" << YAML::Value << cameraComponent.AspectRatio;
			out << YAML::Key << "Projection" << YAML::Value
				<< (cameraComponent.Type == CameraComponent::ProjectionType::Orthographic ? "Orthographic" : "Perspective");
			out << YAML::Key << "OrthographicSize" << YAML::Value << cameraComponent.OrthographicSize;
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
			auto& rb = entity.GetComponent<RigidbodyComponent>();
			out << YAML::Key << "RigidbodyComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "BodyType" << YAML::Value << (int)rb.Type;
			out << YAML::Key << "FixedRotation" << YAML::Value << rb.FixedRotation;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<BoxColliderComponent>())
		{
			auto& bc = entity.GetComponent<BoxColliderComponent>();
			out << YAML::Key << "BoxColliderComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Offset" << YAML::Value << EncodeVec3(bc.Offset);
			out << YAML::Key << "Size" << YAML::Value << EncodeVec3(bc.Size);
			out << YAML::Key << "Density" << YAML::Value << bc.Density;
			out << YAML::Key << "Friction" << YAML::Value << bc.Friction;
			out << YAML::Key << "Restitution" << YAML::Value << bc.Restitution;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<SphereColliderComponent>())
		{
			auto& sc = entity.GetComponent<SphereColliderComponent>();
			out << YAML::Key << "SphereColliderComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Offset" << YAML::Value << EncodeVec3(sc.Offset);
			out << YAML::Key << "Radius" << YAML::Value << sc.Radius;
			out << YAML::Key << "Density" << YAML::Value << sc.Density;
			out << YAML::Key << "Friction" << YAML::Value << sc.Friction;
			out << YAML::Key << "Restitution" << YAML::Value << sc.Restitution;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<LightComponent>())
		{
			auto& light = entity.GetComponent<LightComponent>();
			out << YAML::Key << "LightComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Type" << YAML::Value << (int)light.Type;
			out << YAML::Key << "Color" << YAML::Value << EncodeVec3(light.Color);
			out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
			out << YAML::Key << "Direction" << YAML::Value << EncodeVec3(light.Direction);
			out << YAML::Key << "Constant" << YAML::Value << light.Constant;
			out << YAML::Key << "Linear" << YAML::Value << light.Linear;
			out << YAML::Key << "Quadratic" << YAML::Value << light.Quadratic;
			out << YAML::Key << "CutOff" << YAML::Value << light.CutOff;
			out << YAML::Key << "OuterCutOff" << YAML::Value << light.OuterCutOff;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<PBRMaterialComponent>())
		{
			auto& pbr = entity.GetComponent<PBRMaterialComponent>();
			out << YAML::Key << "PBRMaterialComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Albedo" << YAML::Value << EncodeVec3(pbr.Albedo);
			out << YAML::Key << "Metallic" << YAML::Value << pbr.Metallic;
			out << YAML::Key << "Roughness" << YAML::Value << pbr.Roughness;
			out << YAML::Key << "AO" << YAML::Value << pbr.AO;
			out << YAML::Key << "Emissive" << YAML::Value << EncodeVec3(pbr.Emissive);
			out << YAML::Key << "EmissiveIntensity" << YAML::Value << pbr.EmissiveIntensity;
			out << YAML::Key << "AlbedoTexturePath" << YAML::Value << pbr.AlbedoTexturePath;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<MeshComponent>())
		{
			auto& mesh = entity.GetComponent<MeshComponent>();
			out << YAML::Key << "MeshComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "ModelPath" << YAML::Value << mesh.ModelPath;
			out << YAML::Key << "Scale" << YAML::Value << EncodeVec3(mesh.Scale);
			out << YAML::EndMap;
		}

		if (entity.HasComponent<EnvironmentMapComponent>())
		{
			auto& envMap = entity.GetComponent<EnvironmentMapComponent>();
			out << YAML::Key << "EnvironmentMapComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Path" << YAML::Value << envMap.Path;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<AudioSourceComponent>())
		{
			// Only authored fields: SoundHandle and Finished are per-session state
			// and would be meaningless (and wrong) if written to disk.
			auto& audio = entity.GetComponent<AudioSourceComponent>();
			out << YAML::Key << "AudioSourceComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "SourcePath" << YAML::Value << audio.SourcePath;
			out << YAML::Key << "Volume" << YAML::Value << audio.Volume;
			out << YAML::Key << "Pitch" << YAML::Value << audio.Pitch;
			out << YAML::Key << "Loop" << YAML::Value << audio.Loop;
			out << YAML::Key << "PlayOnStart" << YAML::Value << audio.PlayOnStart;
			out << YAML::Key << "Bus" << YAML::Value << AudioBusName(audio.Bus);
			out << YAML::Key << "Spatial" << YAML::Value << audio.Spatial;
			out << YAML::Key << "MinDistance" << YAML::Value << audio.MinDistance;
			out << YAML::Key << "MaxDistance" << YAML::Value << audio.MaxDistance;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<LabelComponent>())
		{
			const LabelComponent& label = entity.GetComponent<LabelComponent>();

			out << YAML::Key << "LabelComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Text" << YAML::Value << label.Text;
			out << YAML::Key << "Size" << YAML::Value << label.Size;
			out << YAML::Key << "Billboard" << YAML::Value << label.Billboard;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<MultiMeshComponent>())
		{
			const MultiMeshComponent& multi = entity.GetComponent<MultiMeshComponent>();

			out << YAML::Key << "MultiMeshComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "ModelPath" << YAML::Value << multi.ModelPath;
			out << YAML::Key << "Instances" << YAML::Value << YAML::BeginSeq;

			for (const glm::mat4& transform : multi.Transforms)
			{
				out << YAML::BeginSeq;
				for (int column = 0; column < 4; ++column)
				{
					for (int row = 0; row < 4; ++row)
						out << transform[column][row];
				}
				out << YAML::EndSeq;
			}

			out << YAML::EndSeq;
			out << YAML::EndMap;
		}

		if (entity.HasComponent<AnimationComponent>())
		{
			const AnimationComponent& animation = entity.GetComponent<AnimationComponent>();

			out << YAML::Key << "AnimationComponent";
			out << YAML::BeginMap;
			out << YAML::Key << "Time" << YAML::Value << animation.Time;
			out << YAML::Key << "Playing" << YAML::Value << animation.Playing;
			out << YAML::Key << "Speed" << YAML::Value << animation.Speed;
			out << YAML::Key << "Loop" << YAML::Value << animation.Clip.loop;
			out << YAML::Key << "Tracks" << YAML::Value << YAML::BeginSeq;

			for (const AnimationTrack& track : animation.Clip.tracks)
			{
				out << YAML::BeginMap;
				out << YAML::Key << "Property" << YAML::Value << AnimatedPropertyName(track.property);
				out << YAML::Key << "Interpolation" << YAML::Value
					<< (track.interpolation == Interpolation::Smooth ? "Smooth" : "Linear");
				out << YAML::Key << "Keys" << YAML::Value << YAML::BeginSeq;

				for (const AnimationKeyframe& key : track.keys)
				{
					out << YAML::BeginMap;
					out << YAML::Key << "Time" << YAML::Value << key.time;
					out << YAML::Key << "Value" << YAML::Value << EncodeVec3(key.value);
					out << YAML::EndMap;
				}

				out << YAML::EndSeq;
				out << YAML::EndMap;
			}

			out << YAML::EndSeq;
			out << YAML::EndMap;
		}

		out << YAML::EndMap; // Entity
	}

	bool SceneSerializer::Serialize(const std::string& filepath)
	{
		if (filepath.empty())
		{
			FG_CORE_ERROR("Serialize: no filepath given");
			return false;
		}

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Scene" << YAML::Value << "Untitled";
		out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

		// Authoring order, not enTT view order: the file is then stable across
		// save/load cycles, and reloading it reproduces the same entity order.
		for (const Entity& entity : m_Scene->GetEntitiesInOrder())
			SerializeEntity(out, entity);

		out << YAML::EndSeq;
		out << YAML::EndMap;

		// Create parent directories so save/open round-trips work even when the
		// target path lives inside a fresh or empty directory tree.
		//
		// Only when the path actually has a directory component. A bare filename
		// has no separator, so substr(0, npos) would hand the *file name* to
		// create_directories - creating a directory where the file should be,
		// after which opening the file fails and the save is lost.
		const auto separator = filepath.find_last_of('/');
		if (separator != std::string::npos)
		{
			// The error_code overload, deliberately. The throwing one propagates a
			// filesystem_error straight past the caller, so a save that cannot
			// happen raises an exception instead of returning false - which is
			// the one thing a [[nodiscard]] bool exists to prevent, and what an
			// agent driving EditorContext would hit rather than a clean failure.
			std::error_code dirError;
			std::filesystem::create_directories(filepath.substr(0, separator), dirError);

			if (dirError)
			{
				FG_CORE_ERROR("Failed to create directory for '{0}': {1}", filepath, dirError.message());
				return false;
			}
		}

		// FileSystem::WriteTextFile checks the stream after flushing, so a
		// failure on the way to disk (full volume, permissions) is reported
		// rather than leaving a truncated scene file behind a true.
		if (!FileSystem::WriteTextFile(filepath, out.c_str()))
		{
			FG_CORE_ERROR("Failed to write scene file: {0}", filepath);
			return false;
		}

		return true;
	}

	// Reads every component node present on an entity YAML node into `entity`.
	// Shared between full-scene deserialization and prefab instantiation.
	static void DeserializeEntityComponents(Entity deserializedEntity, const YAML::Node& entityNode)
	{
		if (HasNode(entityNode, "TransformComponent"))
		{
			auto transformComponent = entityNode["TransformComponent"];
			auto& tc = deserializedEntity.GetComponent<TransformComponent>();
			tc.Translation = DecodeVec3(transformComponent["Translation"], tc.Translation);
			tc.Rotation = DecodeVec3(transformComponent["Rotation"], tc.Rotation);
			tc.Scale = DecodeVec3(transformComponent["Scale"], tc.Scale);
		}

		if (HasNode(entityNode, "LabelComponent"))
		{
			auto labelNode = entityNode["LabelComponent"];
			auto& label = deserializedEntity.AddComponent<LabelComponent>();
			label.Text = GetString(labelNode, "Text", "");
			label.Size = GetFloat(labelNode, "Size", 0.01f);
			label.Billboard = GetBool(labelNode, "Billboard", true);
		}

		if (HasNode(entityNode, "MultiMeshComponent"))
		{
			auto multiNode = entityNode["MultiMeshComponent"];
			auto& multi = deserializedEntity.AddComponent<MultiMeshComponent>();
			multi.ModelPath = GetString(multiNode, "ModelPath", "");

			// The path is assigned rather than added, so the hook that
			// imports the model does not run - the same contract the
			// MeshComponent has.
			if (!multi.ModelPath.empty())
			{
				multi.CpuMeshData = CpuMesh::LoadFromFile(multi.ModelPath);
				if (!multi.CpuMeshData)
				{
					FG_CORE_ERROR("Entity '{0}': could not import model '{1}'",
						deserializedEntity.GetName(), multi.ModelPath);
				}
			}

			if (HasNode(multiNode, "Instances"))
			{
				for (const YAML::Node& instanceNode : multiNode["Instances"])
				{
					glm::mat4 transform = glm::mat4(1.0f);
					int index = 0;
					for (int column = 0; column < 4; ++column)
					{
						for (int row = 0; row < 4; ++row)
						{
							if (index < static_cast<int>(instanceNode.size()))
								transform[column][row] = instanceNode[index].as<float>();
							index++;
						}
					}
					multi.Transforms.push_back(transform);
				}
			}
		}

		if (HasNode(entityNode, "AnimationComponent"))
		{
			auto animationNode = entityNode["AnimationComponent"];
			auto& animation = deserializedEntity.AddComponent<AnimationComponent>();

			animation.Time = GetFloat(animationNode, "Time", 0.0f);
			animation.Playing = GetBool(animationNode, "Playing", true);
			animation.Speed = GetFloat(animationNode, "Speed", 1.0f);
			animation.Clip.loop = GetBool(animationNode, "Loop", true);

			if (HasNode(animationNode, "Tracks"))
			{
				for (const YAML::Node& trackNode : animationNode["Tracks"])
				{
					AnimationTrack track;

					AnimatedProperty property = AnimatedProperty::Translation;
					if (HasNode(trackNode, "Property"))
						(void)AnimatedPropertyFromName(trackNode["Property"].as<std::string>(), property);
					track.property = property;

					if (HasNode(trackNode, "Interpolation"))
					{
						const std::string interpolation = trackNode["Interpolation"].as<std::string>();
						track.interpolation = interpolation == "Smooth" ? Interpolation::Smooth : Interpolation::Linear;
					}

					if (HasNode(trackNode, "Keys"))
					{
						for (const YAML::Node& keyNode : trackNode["Keys"])
						{
							AnimationKeyframe key;
							key.time = GetFloat(keyNode, "Time", 0.0f);
							key.value = DecodeVec3(keyNode["Value"], glm::vec3(0.0f));
							track.keys.push_back(key);
						}
					}

					animation.Clip.tracks.push_back(track);
				}
			}
		}

		if (HasNode(entityNode, "CameraComponent"))
		{
			auto cameraNode = entityNode["CameraComponent"];
			auto& camera = deserializedEntity.AddOrReplaceComponent<CameraComponent>();
			camera.Primary = GetBool(cameraNode, "Primary", true);
			camera.FieldOfView = GetFloat(cameraNode, "FieldOfView", camera.FieldOfView);
			camera.NearClip = GetFloat(cameraNode, "NearClip", camera.NearClip);
			camera.FarClip = GetFloat(cameraNode, "FarClip", camera.FarClip);
			camera.AspectRatio = GetFloat(cameraNode, "AspectRatio", camera.AspectRatio);
			camera.OrthographicSize = GetFloat(cameraNode, "OrthographicSize", camera.OrthographicSize);

			// A scene written before projection types existed has no Projection
			// key and must keep working: absence means perspective, and a name
			// this build does not know falls back to it rather than failing the
			// whole load.
			const std::string projection = GetString(cameraNode, "Projection", "Perspective");
			camera.Type = projection == "Orthographic"
				? CameraComponent::ProjectionType::Orthographic
				: CameraComponent::ProjectionType::Perspective;
			camera.UpdateProjection();
		}

		if (HasNode(entityNode, "ScriptComponent"))
			deserializedEntity.AddOrReplaceComponent<ScriptComponent>().ClassName = GetString(entityNode["ScriptComponent"], "ClassName");

		if (HasNode(entityNode, "RigidbodyComponent"))
		{
			auto& rb = deserializedEntity.AddOrReplaceComponent<RigidbodyComponent>();
			rb.Type = (RigidbodyComponent::BodyType)GetInt(entityNode["RigidbodyComponent"], "BodyType", 0);
			rb.FixedRotation = GetBool(entityNode["RigidbodyComponent"], "FixedRotation", false);
		}

		if (HasNode(entityNode, "BoxColliderComponent"))
		{
			auto colliderNode = entityNode["BoxColliderComponent"];
			auto& bc = deserializedEntity.AddOrReplaceComponent<BoxColliderComponent>();
			bc.Offset = DecodeVec3(colliderNode["Offset"], bc.Offset);
			bc.Size = DecodeVec3(colliderNode["Size"], bc.Size);
			bc.Density = GetFloat(colliderNode, "Density", bc.Density);
			bc.Friction = GetFloat(colliderNode, "Friction", bc.Friction);
			bc.Restitution = GetFloat(colliderNode, "Restitution", bc.Restitution);
		}

		if (HasNode(entityNode, "SphereColliderComponent"))
		{
			auto colliderNode = entityNode["SphereColliderComponent"];
			auto& sc = deserializedEntity.AddOrReplaceComponent<SphereColliderComponent>();
			sc.Offset = DecodeVec3(colliderNode["Offset"], sc.Offset);
			sc.Radius = GetFloat(colliderNode, "Radius", sc.Radius);
			sc.Density = GetFloat(colliderNode, "Density", sc.Density);
			sc.Friction = GetFloat(colliderNode, "Friction", sc.Friction);
			sc.Restitution = GetFloat(colliderNode, "Restitution", sc.Restitution);
		}

		if (HasNode(entityNode, "LightComponent"))
		{
			auto lightNode = entityNode["LightComponent"];
			auto& light = deserializedEntity.AddOrReplaceComponent<LightComponent>();
			light.Type = (decltype(light.Type))GetInt(lightNode, "Type", (int)light.Type);
			light.Color = DecodeVec3(lightNode["Color"], light.Color);
			light.Intensity = GetFloat(lightNode, "Intensity", light.Intensity);
			light.Direction = DecodeVec3(lightNode["Direction"], light.Direction);
			light.Constant = GetFloat(lightNode, "Constant", light.Constant);
			light.Linear = GetFloat(lightNode, "Linear", light.Linear);
			light.Quadratic = GetFloat(lightNode, "Quadratic", light.Quadratic);
			light.CutOff = GetFloat(lightNode, "CutOff", light.CutOff);
			light.OuterCutOff = GetFloat(lightNode, "OuterCutOff", light.OuterCutOff);
		}

		if (HasNode(entityNode, "PBRMaterialComponent"))
		{
			auto materialNode = entityNode["PBRMaterialComponent"];
			auto& pbr = deserializedEntity.AddOrReplaceComponent<PBRMaterialComponent>();
			pbr.Albedo = DecodeVec3(materialNode["Albedo"], pbr.Albedo);
			pbr.Metallic = GetFloat(materialNode, "Metallic", pbr.Metallic);
			pbr.Roughness = GetFloat(materialNode, "Roughness", pbr.Roughness);
			pbr.AO = GetFloat(materialNode, "AO", pbr.AO);
			pbr.Emissive = DecodeVec3(materialNode["Emissive"], pbr.Emissive);
			pbr.EmissiveIntensity = GetFloat(materialNode, "EmissiveIntensity", pbr.EmissiveIntensity);
			pbr.AlbedoTexturePath = GetString(materialNode, "AlbedoTexturePath");
		}

		if (HasNode(entityNode, "MeshComponent"))
		{
			auto meshNode = entityNode["MeshComponent"];
			auto& mesh = deserializedEntity.AddOrReplaceComponent<MeshComponent>();
			mesh.ModelPath = GetString(meshNode, "ModelPath");
			mesh.Scale = DecodeVec3(meshNode["Scale"], mesh.Scale);

			// The path is assigned rather than added, so the hook that imports
			// the model does not run. Without this, every entity in a loaded
			// scene that named a model rendered a unit cube instead. A failure is
			// logged by the import itself; the scene still loads, because one
			// missing model should not cost the whole scene.
			(void)deserializedEntity.GetScene()->ImportMeshData(deserializedEntity);
		}

		if (HasNode(entityNode, "EnvironmentMapComponent"))
		{
			deserializedEntity.AddOrReplaceComponent<EnvironmentMapComponent>().Path =
				GetString(entityNode["EnvironmentMapComponent"], "Path");
		}

		if (HasNode(entityNode, "AudioSourceComponent"))
		{
			auto audioNode = entityNode["AudioSourceComponent"];
			auto& audio = deserializedEntity.AddOrReplaceComponent<AudioSourceComponent>();
			audio.SourcePath = GetString(audioNode, "SourcePath");
			audio.Volume = GetFloat(audioNode, "Volume", audio.Volume);
			audio.Pitch = GetFloat(audioNode, "Pitch", audio.Pitch);
			audio.Loop = GetBool(audioNode, "Loop", audio.Loop);
			audio.PlayOnStart = GetBool(audioNode, "PlayOnStart", audio.PlayOnStart);

			// Absent or unknown bus names fall back to the component default
			// rather than failing the load: a typo should not lose the whole
			// scene.
			Forge::AudioBus parsedBus = audio.Bus;
			if (ParseAudioBus(GetString(audioNode, "Bus"), parsedBus))
				audio.Bus = parsedBus;
			audio.Spatial = GetBool(audioNode, "Spatial", audio.Spatial);
			audio.MinDistance = GetFloat(audioNode, "MinDistance", audio.MinDistance);
			audio.MaxDistance = GetFloat(audioNode, "MaxDistance", audio.MaxDistance);
		}
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
		catch (const YAML::BadFile&)
		{
			FG_CORE_ERROR("Scene file not found: {0}", filepath);
			return false;
		}

		if (!data["Scene"])
			return false;

		std::string sceneName = data["Scene"].as<std::string>("Untitled");
		FG_CORE_TRACE("Deserializing scene '{0}'", sceneName);

		auto entities = data["Entities"];
		if (!entities)
			return true;

		for (auto entityNode : entities)
		{
			uint64_t uuid = entityNode["Entity"].as<uint64_t>();

			std::string name = "Entity";
			if (HasNode(entityNode, "TagComponent"))
				name = entityNode["TagComponent"]["Tag"].as<std::string>();

			Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);
			DeserializeEntityComponents(deserializedEntity, entityNode);
		}

		return true;
	}

	// ---------------------------------------------------------------------
	// Runtime (binary) serialization
	//
	// The runtime format is a compact little-endian binary stream used when
	// shipping a game. It has no external dependencies and is intentionally
	// versioned with a magic header so mismatched files fail fast.
	// ---------------------------------------------------------------------

	namespace {

		constexpr uint32_t kRuntimeMagic = 0x46475254; // 'FGRT'
		constexpr uint32_t kRuntimeVersion = 1;

		template<typename T>
		void Write(std::ostream& stream, const T& value)
		{
			static_assert(std::is_trivially_copyable_v<T>, "Runtime serialization requires trivially copyable types");
			stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
		}

		void WriteString(std::ostream& stream, const std::string& value)
		{
			Write<uint32_t>(stream, (uint32_t)value.size());
			if (!value.empty())
				stream.write(value.data(), value.size());
		}

		template<typename T>
		bool Read(std::istream& stream, T& value)
		{
			stream.read(reinterpret_cast<char*>(&value), sizeof(T));
			return stream.good();
		}

		bool ReadString(std::istream& stream, std::string& value)
		{
			uint32_t size = 0;
			if (!Read(stream, size))
				return false;

			value.resize(size);
			if (size > 0)
				stream.read(value.data(), size);

			return stream.good();
		}

		void WriteVec3(std::ostream& stream, const glm::vec3& v)
		{
			Write<float>(stream, v.x);
			Write<float>(stream, v.y);
			Write<float>(stream, v.z);
		}

		glm::vec3 ReadVec3(std::istream& stream)
		{
			glm::vec3 v(0.0f);
			Read<float>(stream, v.x);
			Read<float>(stream, v.y);
			Read<float>(stream, v.z);
			return v;
		}

		void WriteVec4(std::ostream& stream, const glm::vec4& v)
		{
			Write<float>(stream, v.x);
			Write<float>(stream, v.y);
			Write<float>(stream, v.z);
			Write<float>(stream, v.w);
		}

		glm::vec4 ReadVec4(std::istream& stream)
		{
			glm::vec4 v(0.0f);
			Read<float>(stream, v.x);
			Read<float>(stream, v.y);
			Read<float>(stream, v.z);
			Read<float>(stream, v.w);
			return v;
		}

		constexpr uint32_t FlagTag = BIT(0);
		constexpr uint32_t FlagTransform = BIT(1);
		constexpr uint32_t FlagCamera = BIT(2);
		constexpr uint32_t FlagScript = BIT(3);
		constexpr uint32_t FlagRigidbody = BIT(4);
		constexpr uint32_t FlagBoxCollider = BIT(5);
		constexpr uint32_t FlagSphereCollider = BIT(6);
		constexpr uint32_t FlagLight = BIT(7);
		constexpr uint32_t FlagPBR = BIT(8);
		constexpr uint32_t FlagMesh = BIT(9);
		constexpr uint32_t FlagEnvMap = BIT(10);
		constexpr uint32_t FlagAudio = BIT(11);

		// Optional field groups inside a component. The per-component payloads
		// above are a fixed layout, so a field added to one of them would make
		// every previously written file unreadable. Each addition therefore gets
		// its own bit: an old file simply has none of them set and reads back
		// with the component's defaults, which is the whole reason a version
		// bump was not needed.
		constexpr uint32_t FlagCameraProjection = BIT(12);
		constexpr uint32_t FlagPBRAlbedoTexture = BIT(13);
		// The animation is a component of its own, and its payload is a
		// variable-length one: the number of tracks and the number of keys
		// are written with it, so the reader knows how much to read.
		constexpr uint32_t FlagAnimation = BIT(14);
		constexpr uint32_t FlagMultiMesh = BIT(15);

		uint32_t GetComponentFlags(Entity entity)
		{
			uint32_t flags = 0;
			flags |= entity.HasComponent<TagComponent>() ? FlagTag : 0;
			flags |= entity.HasComponent<TransformComponent>() ? FlagTransform : 0;
			flags |= entity.HasComponent<CameraComponent>() ? FlagCamera : 0;
			flags |= entity.HasComponent<ScriptComponent>() ? FlagScript : 0;
			flags |= entity.HasComponent<RigidbodyComponent>() ? FlagRigidbody : 0;
			flags |= entity.HasComponent<BoxColliderComponent>() ? FlagBoxCollider : 0;
			flags |= entity.HasComponent<SphereColliderComponent>() ? FlagSphereCollider : 0;
			flags |= entity.HasComponent<LightComponent>() ? FlagLight : 0;
			flags |= entity.HasComponent<PBRMaterialComponent>() ? FlagPBR : 0;
			flags |= entity.HasComponent<MeshComponent>() ? FlagMesh : 0;
			flags |= entity.HasComponent<EnvironmentMapComponent>() ? FlagEnvMap : 0;
			flags |= entity.HasComponent<AudioSourceComponent>() ? FlagAudio : 0;
			flags |= (entity.HasComponent<CameraComponent>()
				&& entity.GetComponent<CameraComponent>().Type != CameraComponent::ProjectionType::Perspective)
				? FlagCameraProjection : 0;
			flags |= (entity.HasComponent<PBRMaterialComponent>()
				&& !entity.GetComponent<PBRMaterialComponent>().AlbedoTexturePath.empty())
				? FlagPBRAlbedoTexture : 0;
			flags |= entity.HasComponent<AnimationComponent>() ? FlagAnimation : 0;
			flags |= entity.HasComponent<MultiMeshComponent>() ? FlagMultiMesh : 0;
			return flags;
		}

	}

	void SceneSerializer::SerializeRuntime(const std::string& filepath)
	{
		std::ofstream stream(filepath, std::ios::binary);
		if (!stream.is_open())
		{
			FG_CORE_ERROR("Failed to open runtime scene file for writing: {0}", filepath);
			return;
		}

		Write<uint32_t>(stream, kRuntimeMagic);
		Write<uint32_t>(stream, kRuntimeVersion);

		uint32_t entityCount = 0;
		for (auto entityID : m_Scene->m_Registry.view<IDComponent>())
		{
			Entity entity{ entityID, m_Scene.get() };
			if (entity)
				entityCount++;
		}

		Write<uint32_t>(stream, entityCount);

		for (auto entityID : m_Scene->m_Registry.view<IDComponent>())
		{
			Entity entity{ entityID, m_Scene.get() };
			if (!entity)
				continue;

			const uint32_t flags = GetComponentFlags(entity);

			Write<uint64_t>(stream, (uint64_t)entity.GetUUID());
			Write<uint32_t>(stream, flags);

			if (entity.HasComponent<TagComponent>())
				WriteString(stream, entity.GetComponent<TagComponent>().Tag);

			if (entity.HasComponent<TransformComponent>())
			{
				auto& tc = entity.GetComponent<TransformComponent>();
				WriteVec3(stream, tc.Translation);
				WriteVec3(stream, tc.Rotation);
				WriteVec3(stream, tc.Scale);
			}

			if (entity.HasComponent<CameraComponent>())
			{
				auto& camera = entity.GetComponent<CameraComponent>();
				Write<bool>(stream, camera.Primary);

				// Only a non-default projection needs the extra fields written,
				// so a perspective camera costs exactly what it always did.
				if (flags & FlagCameraProjection)
				{
					Write<int>(stream, (int)camera.Type);
					Write<float>(stream, camera.FieldOfView);
					Write<float>(stream, camera.NearClip);
					Write<float>(stream, camera.FarClip);
					Write<float>(stream, camera.AspectRatio);
					Write<float>(stream, camera.OrthographicSize);
				}
			}

			if (entity.HasComponent<ScriptComponent>())
				WriteString(stream, entity.GetComponent<ScriptComponent>().ClassName);

			if (entity.HasComponent<RigidbodyComponent>())
			{
				auto& rb = entity.GetComponent<RigidbodyComponent>();
				Write<int>(stream, (int)rb.Type);
				Write<bool>(stream, rb.FixedRotation);
			}

			if (entity.HasComponent<BoxColliderComponent>())
			{
				auto& bc = entity.GetComponent<BoxColliderComponent>();
				WriteVec3(stream, bc.Offset);
				WriteVec3(stream, bc.Size);
				Write<float>(stream, bc.Density);
				Write<float>(stream, bc.Friction);
				Write<float>(stream, bc.Restitution);
			}

			if (entity.HasComponent<SphereColliderComponent>())
			{
				auto& sc = entity.GetComponent<SphereColliderComponent>();
				WriteVec3(stream, sc.Offset);
				Write<float>(stream, sc.Radius);
				Write<float>(stream, sc.Density);
				Write<float>(stream, sc.Friction);
				Write<float>(stream, sc.Restitution);
			}

			if (entity.HasComponent<LightComponent>())
			{
				auto& light = entity.GetComponent<LightComponent>();
				Write<int>(stream, (int)light.Type);
				WriteVec3(stream, light.Color);
				Write<float>(stream, light.Intensity);
				WriteVec3(stream, light.Direction);
				Write<float>(stream, light.Constant);
				Write<float>(stream, light.Linear);
				Write<float>(stream, light.Quadratic);
				Write<float>(stream, light.CutOff);
				Write<float>(stream, light.OuterCutOff);
			}

			if (entity.HasComponent<PBRMaterialComponent>())
			{
				auto& pbr = entity.GetComponent<PBRMaterialComponent>();
				WriteVec3(stream, pbr.Albedo);
				Write<float>(stream, pbr.Metallic);
				Write<float>(stream, pbr.Roughness);
				Write<float>(stream, pbr.AO);
				WriteVec3(stream, pbr.Emissive);
				Write<float>(stream, pbr.EmissiveIntensity);

				// The albedo texture is what makes a material recognisable at
				// run time; a runtime scene that dropped it rendered every
				// object in flat colour.
				if (flags & FlagPBRAlbedoTexture)
					WriteString(stream, pbr.AlbedoTexturePath);
			}

			if (entity.HasComponent<MeshComponent>())
			{
				auto& mesh = entity.GetComponent<MeshComponent>();
				WriteString(stream, mesh.ModelPath);
				WriteVec3(stream, mesh.Scale);
			}

			if (entity.HasComponent<EnvironmentMapComponent>())
			{
				WriteString(stream, entity.GetComponent<EnvironmentMapComponent>().Path);
			}

			if (entity.HasComponent<AudioSourceComponent>())
			{
				// Authored fields only, exactly like the YAML path: a handle or a
				// finished flag written to disk would be a lie about the next
				// session's state.
				auto& audio = entity.GetComponent<AudioSourceComponent>();
				WriteString(stream, audio.SourcePath);
				Write<float>(stream, audio.Volume);
				Write<float>(stream, audio.Pitch);
				Write<bool>(stream, audio.Loop);
				Write<bool>(stream, audio.PlayOnStart);
				Write<int>(stream, (int)audio.Bus);
				Write<bool>(stream, audio.Spatial);
				Write<float>(stream, audio.MinDistance);
				Write<float>(stream, audio.MaxDistance);
			}

			if (entity.HasComponent<MultiMeshComponent>())
			{
				const MultiMeshComponent& multi = entity.GetComponent<MultiMeshComponent>();
				WriteString(stream, multi.ModelPath);
				Write<uint32_t>(stream, (uint32_t)multi.Transforms.size());
				for (const glm::mat4& transform : multi.Transforms)
					WriteVec4(stream, glm::vec4(transform[0]));
				for (const glm::mat4& transform : multi.Transforms)
					WriteVec4(stream, glm::vec4(transform[1]));
				for (const glm::mat4& transform : multi.Transforms)
					WriteVec4(stream, glm::vec4(transform[2]));
				for (const glm::mat4& transform : multi.Transforms)
					WriteVec4(stream, glm::vec4(transform[3]));
			}

			if (entity.HasComponent<AnimationComponent>())
			{
				// The animation's payload is a variable-length one, so the
				// counts are written with it: the reader knows how much to
				// read without guessing.
				const AnimationComponent& animation = entity.GetComponent<AnimationComponent>();
				Write<float>(stream, animation.Time);
				Write<bool>(stream, animation.Playing);
				Write<float>(stream, animation.Speed);
				Write<bool>(stream, animation.Clip.loop);
				Write<uint32_t>(stream, (uint32_t)animation.Clip.tracks.size());

				for (const AnimationTrack& track : animation.Clip.tracks)
				{
					Write<int>(stream, (int)track.property);
					Write<int>(stream, (int)track.interpolation);
					Write<uint32_t>(stream, (uint32_t)track.keys.size());

					for (const AnimationKeyframe& key : track.keys)
					{
						Write<float>(stream, key.time);
						WriteVec3(stream, key.value);
					}
				}
			}
		}
	}

	bool SceneSerializer::DeserializeRuntime(const std::string& filepath)
	{
		std::ifstream stream(filepath, std::ios::binary);
		if (!stream.is_open())
		{
			FG_CORE_ERROR("Failed to open runtime scene file for reading: {0}", filepath);
			return false;
		}

		uint32_t magic = 0;
		uint32_t version = 0;
		if (!Read<uint32_t>(stream, magic) || magic != kRuntimeMagic)
		{
			FG_CORE_ERROR("Invalid runtime scene file (bad magic): {0}", filepath);
			return false;
		}

		if (!Read<uint32_t>(stream, version) || version != kRuntimeVersion)
		{
			FG_CORE_ERROR("Unsupported runtime scene version {0}: {1}", version, filepath);
			return false;
		}

		uint32_t entityCount = 0;
		if (!Read<uint32_t>(stream, entityCount))
			return false;

		for (uint32_t i = 0; i < entityCount; i++)
		{
			uint64_t uuid = 0;
			uint32_t flags = 0;
			Read<uint64_t>(stream, uuid);
			Read<uint32_t>(stream, flags);

			std::string name = "Entity";
			if (flags & FlagTag)
				ReadString(stream, name);

			Entity entity = m_Scene->CreateEntityWithUUID(uuid, name);

			if (flags & FlagTransform)
			{
				auto& tc = entity.GetComponent<TransformComponent>();
				tc.Translation = ReadVec3(stream);
				tc.Rotation = ReadVec3(stream);
				tc.Scale = ReadVec3(stream);
			}

			if (flags & FlagCamera)
			{
				auto& camera = entity.AddComponent<CameraComponent>();
				Read<bool>(stream, camera.Primary);

				if (flags & FlagCameraProjection)
				{
					int type = (int)CameraComponent::ProjectionType::Perspective;
					Read<int>(stream, type);
					camera.Type = (CameraComponent::ProjectionType)type;
					Read<float>(stream, camera.FieldOfView);
					Read<float>(stream, camera.NearClip);
					Read<float>(stream, camera.FarClip);
					Read<float>(stream, camera.AspectRatio);
					Read<float>(stream, camera.OrthographicSize);
					camera.UpdateProjection();
				}
			}

			if (flags & FlagScript)
				ReadString(stream, entity.AddComponent<ScriptComponent>().ClassName);

			if (flags & FlagRigidbody)
			{
				auto& rb = entity.AddComponent<RigidbodyComponent>();
				int type = 0;
				Read<int>(stream, type);
				rb.Type = (RigidbodyComponent::BodyType)type;
				Read<bool>(stream, rb.FixedRotation);
			}

			if (flags & FlagBoxCollider)
			{
				auto& bc = entity.AddComponent<BoxColliderComponent>();
				bc.Offset = ReadVec3(stream);
				bc.Size = ReadVec3(stream);
				Read<float>(stream, bc.Density);
				Read<float>(stream, bc.Friction);
				Read<float>(stream, bc.Restitution);
			}

			if (flags & FlagSphereCollider)
			{
				auto& sc = entity.AddComponent<SphereColliderComponent>();
				sc.Offset = ReadVec3(stream);
				Read<float>(stream, sc.Radius);
				Read<float>(stream, sc.Density);
				Read<float>(stream, sc.Friction);
				Read<float>(stream, sc.Restitution);
			}

			if (flags & FlagLight)
			{
				auto& light = entity.AddComponent<LightComponent>();
				int type = 0;
				Read<int>(stream, type);
				light.Type = (decltype(light.Type))type;
				light.Color = ReadVec3(stream);
				Read<float>(stream, light.Intensity);
				light.Direction = ReadVec3(stream);
				Read<float>(stream, light.Constant);
				Read<float>(stream, light.Linear);
				Read<float>(stream, light.Quadratic);
				Read<float>(stream, light.CutOff);
				Read<float>(stream, light.OuterCutOff);
			}

			if (flags & FlagPBR)
			{
				auto& pbr = entity.AddComponent<PBRMaterialComponent>();
				pbr.Albedo = ReadVec3(stream);
				Read<float>(stream, pbr.Metallic);
				Read<float>(stream, pbr.Roughness);
				Read<float>(stream, pbr.AO);
				pbr.Emissive = ReadVec3(stream);
				Read<float>(stream, pbr.EmissiveIntensity);

				if (flags & FlagPBRAlbedoTexture)
					ReadString(stream, pbr.AlbedoTexturePath);
			}

			if (flags & FlagMesh)
			{
				auto& mesh = entity.AddComponent<MeshComponent>();
				ReadString(stream, mesh.ModelPath);
				mesh.Scale = ReadVec3(stream);

				// Same as the YAML path: the model is imported by name here,
				// because a component added before its path was known had
				// nothing to import.
				(void)m_Scene->ImportMeshData(entity);
			}

			if (flags & FlagEnvMap)
			{
				ReadString(stream, entity.AddComponent<EnvironmentMapComponent>().Path);
			}

			if (flags & FlagAudio)
			{
				auto& audio = entity.AddComponent<AudioSourceComponent>();
				ReadString(stream, audio.SourcePath);
				Read<float>(stream, audio.Volume);
				Read<float>(stream, audio.Pitch);
				Read<bool>(stream, audio.Loop);
				Read<bool>(stream, audio.PlayOnStart);
				int bus = (int)AudioBus::SoundEffects;
				Read<int>(stream, bus);
				audio.Bus = (AudioBus)bus;
				Read<bool>(stream, audio.Spatial);
				Read<float>(stream, audio.MinDistance);
				Read<float>(stream, audio.MaxDistance);
			}

			if (flags & FlagMultiMesh)
			{
				auto& multi = entity.AddComponent<MultiMeshComponent>();
				ReadString(stream, multi.ModelPath);

				uint32_t instanceCount = 0;
				Read<uint32_t>(stream, instanceCount);

				multi.Transforms.resize(instanceCount);
				for (uint32_t i = 0; i < instanceCount; ++i)
					multi.Transforms[i][0] = ReadVec4(stream);
				for (uint32_t i = 0; i < instanceCount; ++i)
					multi.Transforms[i][1] = ReadVec4(stream);
				for (uint32_t i = 0; i < instanceCount; ++i)
					multi.Transforms[i][2] = ReadVec4(stream);
				for (uint32_t i = 0; i < instanceCount; ++i)
					multi.Transforms[i][3] = ReadVec4(stream);
			}

			if (flags & FlagAnimation)
			{
				auto& animation = entity.AddComponent<AnimationComponent>();
				Read<float>(stream, animation.Time);
				Read<bool>(stream, animation.Playing);
				Read<float>(stream, animation.Speed);
				Read<bool>(stream, animation.Clip.loop);

				uint32_t trackCount = 0;
				Read<uint32_t>(stream, trackCount);
				for (uint32_t t = 0; t < trackCount; ++t)
				{
					AnimationTrack track;
					int property = (int)AnimatedProperty::Translation;
					Read<int>(stream, property);
					track.property = (AnimatedProperty)property;
					int interpolation = (int)Interpolation::Linear;
					Read<int>(stream, interpolation);
					track.interpolation = (Interpolation)interpolation;

					uint32_t keyCount = 0;
					Read<uint32_t>(stream, keyCount);
					for (uint32_t k = 0; k < keyCount; ++k)
					{
						AnimationKeyframe key;
						Read<float>(stream, key.time);
						key.value = ReadVec3(stream);
						track.keys.push_back(key);
					}

					animation.Clip.tracks.push_back(track);
				}
			}
		}

		return true;
	}

	bool SceneSerializer::SerializeEntityToFile(Entity entity, const std::string& filepath)
	{
		if (!entity || !entity.HasComponent<IDComponent>())
		{
			FG_CORE_ERROR("SerializeEntityToFile: entity is invalid");
			return false;
		}

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Prefab" << YAML::Value << entity.GetName();
		out << YAML::Key << "Entity" << YAML::Value;
		SerializeEntity(out, entity);
		out << YAML::EndMap;

		if (!FileSystem::WriteTextFile(filepath, out.c_str()))
		{
			FG_CORE_ERROR("Failed to write prefab file: {0}", filepath);
			return false;
		}

		return true;
	}

	std::vector<std::string> SceneSerializer::GetStateComponents(const std::string& state)
	{
		std::vector<std::string> names;
		if (state.empty())
			return names;

		YAML::Node data;
		try
		{
			data = YAML::Load(state);
		}
		catch (const YAML::Exception&)
		{
			return names;
		}

		YAML::Node entityNode = data["Entity"];
		if (!entityNode)
			return names;

		for (const auto& entry : entityNode)
		{
			const std::string key = entry.first.as<std::string>();
			// The identity is not a component.
			if (key != "Entity")
				names.push_back(key);
		}

		return names;
	}

	bool SceneSerializer::ApplyEntityState(Entity entity, const std::string& state)
	{
		if (!entity || state.empty())
			return false;

		YAML::Node data;
		try
		{
			data = YAML::Load(state);
		}
		catch (const YAML::Exception& e)
		{
			FG_CORE_ERROR("ApplyEntityState: '{0}'", e.what());
			return false;
		}

		YAML::Node entityNode = data["Entity"];
		if (!entityNode || !IsEntityNode(entityNode))
			return false;

		DeserializeEntityComponents(entity, entityNode);
		return true;
	}

	std::string SceneSerializer::SerializeEntityToString(Entity entity)
	{
		if (!entity || !entity.HasComponent<IDComponent>())
		{
			FG_CORE_ERROR("SerializeEntityToString: entity is invalid");
			return {};
		}

		YAML::Emitter out;
		out << YAML::BeginMap;
		out << YAML::Key << "Entity" << YAML::Value;
		SerializeEntity(out, entity);
		out << YAML::EndMap;

		return out.c_str();
	}

	Entity SceneSerializer::DeserializeEntityFromString(Scene* scene, const std::string& text)
	{
		if (!scene || text.empty())
			return {};

		YAML::Node data;
		try
		{
			data = YAML::Load(text);
		}
		catch (const YAML::Exception& e)
		{
			FG_CORE_ERROR("DeserializeEntityFromString: '{0}'", e.what());
			return {};
		}

		YAML::Node entityNode = data["Entity"];
		if (!entityNode || !IsEntityNode(entityNode))
		{
			FG_CORE_ERROR("DeserializeEntityFromString: no recognisable entity components");
			return {};
		}

		// The identity in the text is the entity's own, so an undone edit
		// restores the entity that was edited - the scripts that name it by
		// UUID keep naming it.
		uint64_t uuidValue = 0;
		if (HasNode(entityNode, "Entity"))
		{
			try
			{
				uuidValue = entityNode["Entity"].as<uint64_t>();
			}
			catch (const YAML::Exception&)
			{
				uuidValue = 0;
			}
		}
		const UUID uuid(uuidValue);

		std::string name = "Entity";
		if (HasNode(entityNode, "TagComponent"))
			name = entityNode["TagComponent"]["Tag"].as<std::string>();

		Entity entity = uuidValue != 0 ? scene->CreateEntityWithUUID(uuid, name) : scene->CreateEntity(name);
		DeserializeEntityComponents(entity, entityNode);
		return entity;
	}

	bool SceneSerializer::IsEntityFile(const std::string& filepath)
	{
		if (filepath.empty())
			return false;

		try
		{
			const YAML::Node data = YAML::LoadFile(filepath);
			if (!data)
				return false;

			return IsEntityNode(data["Entity"]);
		}
		catch (const YAML::Exception&)
		{
			return false;
		}
	}

	Entity SceneSerializer::DeserializeEntityFromFile(Scene* scene, const std::string& filepath,
		bool usePositionOverride, const glm::vec3& positionOverride)
	{
		if (!scene)
			return {};

		YAML::Node data;
		try
		{
			data = YAML::LoadFile(filepath);
		}
		catch (const YAML::Exception& e)
		{
			FG_CORE_ERROR("Failed to load prefab file '{0}': {1}", filepath, e.what());
			return {};
		}

		YAML::Node entityNode = data["Entity"];
		if (!entityNode)
		{
			FG_CORE_ERROR("Prefab file has no 'Entity' node: {0}", filepath);
			return {};
		}

		// A file can parse as YAML and still not be an entity file, so require
		// the actual entity payload rather than trusting the parse.
		if (!IsEntityNode(entityNode))
		{
			FG_CORE_ERROR("Prefab file has no recognisable entity components: {0}", filepath);
			return {};
		}

		std::string name = "Entity";
		if (HasNode(entityNode, "TagComponent"))
			name = entityNode["TagComponent"]["Tag"].as<std::string>();

		// Prefabs instantiate with a fresh identity so every spawn is unique.
		Entity entity = scene->CreateEntity(name);
		DeserializeEntityComponents(entity, entityNode);

		if (usePositionOverride && entity.HasComponent<TransformComponent>())
			entity.GetComponent<TransformComponent>().Translation = positionOverride;

		return entity;
	}

}
