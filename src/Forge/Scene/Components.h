#pragma once

#include "Forge/Core/UUID.h"
#include "Forge/Renderer/Texture.h"
#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Scene/Animation.h"
#include "Forge/UI/Label.h"
#include "Forge/Physics/CharacterController.h"
#include "Forge/Audio/AudioEngine.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Forge {

	struct IDComponent
	{
		UUID ID;

		IDComponent() = default;
		IDComponent(const IDComponent&) = default;
		IDComponent(const UUID& id) : ID(id) {}
	};

	struct TagComponent
	{
		std::string Tag;

		TagComponent() = default;
		TagComponent(const TagComponent&) = default;
		TagComponent(const std::string& tag)
			: Tag(tag) {}
	};

	struct TransformComponent
	{
		glm::vec3 Translation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::vec3& translation)
			: Translation(translation) {}

		glm::mat4 GetTransform() const
		{
			glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));

			return glm::translate(glm::mat4(1.0f), Translation)
				* rotation
				* glm::scale(glm::mat4(1.0f), Scale);
		}
	};

	struct CameraComponent
	{
		// The Hazel SceneCamera projection type. Perspective is a 3D world seen
		// from a point; orthographic is parallel projection, which is what a 2D
		// overlay, a minimap or an isometric view needs. Without it a scene has
		// no way to express "show this without perspective", and the renderer
		// was perspective-only as a consequence.
		enum class ProjectionType { Perspective = 0, Orthographic = 1 };

		glm::mat4 ProjectionMatrix;
		glm::mat4 ViewMatrix;
		glm::mat4 ViewProjectionMatrix;

		bool Primary = true;

		// Projection parameters (kept so the projection can be rebuilt on resize).
		ProjectionType Type = ProjectionType::Perspective;
		float FieldOfView = 45.0f;   // degrees, perspective only
		float NearClip = 0.1f;
		float FarClip = 1000.0f;
		float AspectRatio = 16.0f / 9.0f;

		// Vertical extent of the orthographic frustum in world units. The
		// horizontal extent follows from AspectRatio, so the frustum keeps the
		// viewport's shape instead of stretching the view to fit it.
		float OrthographicSize = 10.0f;

		CameraComponent() = default;
		CameraComponent(const CameraComponent&) = default;

		// (Re)build the projection matrix from the stored parameters.
		void UpdateProjection()
		{
			if (Type == ProjectionType::Orthographic)
			{
				const float halfHeight = OrthographicSize * 0.5f;
				const float halfWidth = halfHeight * AspectRatio;
				ProjectionMatrix = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, NearClip, FarClip);
				return;
			}

			ProjectionMatrix = glm::perspective(glm::radians(FieldOfView), AspectRatio, NearClip, FarClip);
		}
	};

	struct ScriptComponent
	{
		std::string ClassName;

		ScriptComponent() = default;
		ScriptComponent(const ScriptComponent&) = default;
	};

	struct NativeScriptComponent
	{
		class ScriptableEntity* Instance = nullptr;

		ScriptableEntity*(*InstantiateScript)();
		void (*DestroyScript)(NativeScriptComponent*);

		template<typename T>
		void Bind()
		{
			InstantiateScript = []() { return static_cast<ScriptableEntity*>(new T()); };
			DestroyScript = [](NativeScriptComponent* nsc) { delete nsc->Instance; nsc->Instance = nullptr; };
		}
	};

	// Physics

	struct RigidbodyComponent
	{
		enum class BodyType { Static = 0, Dynamic, Kinematic };
		BodyType Type = BodyType::Static;
		bool FixedRotation = false;

		// Storage for runtime
		void* RuntimeBody = nullptr;

		RigidbodyComponent() = default;
		RigidbodyComponent(const RigidbodyComponent&) = default;
	};

	struct BoxColliderComponent
	{
		glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
		glm::vec3 Size = { 1.0f, 1.0f, 1.0f };

		// Physics material
		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		BoxColliderComponent() = default;
		BoxColliderComponent(const BoxColliderComponent&) = default;
	};

	struct SphereColliderComponent
	{
		glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
		float Radius = 0.5f;

		// Physics material
		float Density = 1.0f;
		float Friction = 0.5f;
		float Restitution = 0.0f;

		// Storage for runtime
		void* RuntimeFixture = nullptr;

		SphereColliderComponent() = default;
		SphereColliderComponent(const SphereColliderComponent&) = default;
	};

	// 3D Rendering

	struct MeshComponent
	{
		std::string ModelPath;

		// The imported geometry, loaded on the CPU when the component is added.
		//
		// MeshComponent::ModelPath used to be written by the serializer and read
		// back, and then never used: nothing loaded it, so an entity saved from
		// a model rendered a unit cube. Importing here - on the component, not
		// in the renderer - is what makes the path mean something, and it also
		// means a broken model path is reported when the entity is created
		// rather than silently on some later frame.
		Ref<class CpuMesh> CpuMeshData;

		// The uploaded mesh. Filled in by the renderer from CpuMeshData, which
		// is the only part of this that needs a graphics device - and is
		// therefore not something the engine can do headlessly.
		Ref<class Mesh> Mesh;

		glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };

		// The lower-detail versions of the mesh, and the distances at which
		// they take over.
		//
		// A mesh that is far away is drawn with fewer triangles: the
		// difference between the levels cannot be seen at that distance, and
		// the triangles that are not drawn are not paid for. The levels are
		// ordered from the most detailed to the least.
		std::vector<std::string> LODModelPaths;
		std::vector<float> LODSwitchDistances;

		// The imported geometry of the levels, parallel to the paths.
		std::vector<Ref<class CpuMesh>> LODCpuMeshData;

		MeshComponent() = default;
		MeshComponent(const MeshComponent&) = default;
	};

	// A mesh drawn many times: one mesh, many transforms, one draw call.
	//
	// A forest is a thousand draw calls when every tree is an entity, and
	// one when the trees are instances of one mesh. The transforms are the
	// instances' places in the world; the entity's own transform is applied
	// to all of them, so the whole group can be moved as one.
	struct MultiMeshComponent
	{
		// The mesh the instances share.
		std::string ModelPath;

		// The imported geometry, loaded on the CPU when the component is
		// added - the same contract the MeshComponent has.
		Ref<class CpuMesh> CpuMeshData;

		// The uploaded mesh. Filled in by the renderer.
		Ref<class Mesh> Mesh;

		// Where the instances are.
		std::vector<glm::mat4> Transforms;

		MultiMeshComponent() = default;
		MultiMeshComponent(const MultiMeshComponent&) = default;
	};

	struct PBRMaterialComponent
	{
		glm::vec3 Albedo = { 1.0f, 1.0f, 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float AO = 1.0f;
		glm::vec3 Emissive = { 0.0f, 0.0f, 0.0f };
		float EmissiveIntensity = 0.0f;

		std::string AlbedoTexturePath;

		PBRMaterialComponent() = default;
		PBRMaterialComponent(const PBRMaterialComponent&) = default;
	};

	struct LightComponent
	{
		enum class Type { Directional, Point, Spot };
		Type Type = Type::Directional;

		glm::vec3 Color = { 1.0f, 1.0f, 1.0f };
		float Intensity = 1.0f;

		// Directional
		glm::vec3 Direction = { 0.0f, -1.0f, 0.0f };

		// Point
		float Constant = 1.0f;
		float Linear = 0.09f;
		float Quadratic = 0.032f;

		// Spot
		float CutOff = glm::cos(glm::radians(12.5f));
		float OuterCutOff = glm::cos(glm::radians(17.5f));

		LightComponent() = default;
		LightComponent(const LightComponent&) = default;
	};

	struct EnvironmentMapComponent
	{
		// Path to an HDR environment map (equirectangular panorama).
		std::string Path;

		EnvironmentMapComponent() = default;
		EnvironmentMapComponent(const EnvironmentMapComponent&) = default;
		EnvironmentMapComponent(const std::string& path) : Path(path) {}
	};

	// Plays a sound clip. Authored fields are serialized; the runtime fields
	// below are rebuilt by the Scene on playback and never written to disk.
	struct AudioSourceComponent
	{
		// Clip to play, relative to the working directory. A source with an empty
		// path is inert rather than an error, so a half-finished entity still
		// simulates.
		std::string SourcePath;

		float Volume = 1.0f;
		float Pitch = 1.0f;

		// Looping sounds keep playing until stopped; one-shot sounds finish and
		// are then reclaimed.
		bool Loop = false;

		// Start playing automatically when playback begins.
		bool PlayOnStart = true;

		// Mixing bus this source routes through. Lowering a bus's volume ducks
		// every source on it in one call, rather than iterating each one.
		AudioBus Bus = AudioBus::SoundEffects;

		// 3D audio. When enabled the sound's world position comes from its
		// TransformComponent and it is attenuated and panned relative to the
		// listener (the primary camera). When disabled it plays at full volume
		// from the centre, which is right for music and UI.
		bool Spatial = false;
		float MinDistance = 1.0f;
		float MaxDistance = 20.0f;

		// --- Runtime only (not serialized) ---

		// Handle of the currently playing sound, or 0 when not playing.
		uint64_t SoundHandle = 0;

		// True once the sound has been observed to finish, so a one-shot can be
		// detected and its handle released rather than leaking for the session.
		bool Finished = false;

		AudioSourceComponent() = default;
		AudioSourceComponent(const AudioSourceComponent&) = default;
		AudioSourceComponent(const std::string& path) : SourcePath(path) {}
	};

	// A keyframed animation played on the entity.
	//
	// The clip is the data - the tracks, the keyframes, the times - and the
	// component is the player: it holds the time the clip is at and whether
	// it is playing. The scene advances the time and writes the values onto
	// the entity every frame.
	struct AnimationComponent
	{
		// The clip that is played. A clip with no tracks is inert: the
		// entity is left as it is.
		AnimationClip Clip;

		// Where the clip is, in seconds.
		float Time = 0.0f;

		// Whether the clip advances. A clip that has run out and does not
		// loop stops playing and holds its last value.
		bool Playing = true;

		// How fast the clip runs. Negative runs it backwards.
		float Speed = 1.0f;

		AnimationComponent() = default;
		AnimationComponent(const AnimationComponent&) = default;
		explicit AnimationComponent(const AnimationClip& clip) : Clip(clip) {}
	};

	// A character: a body that walks.
	//
	// The game says how fast it walks; the controller moves the entity and
	// resolves the move against the world's static geometry. It is not a
	// rigid body: a character is moved by the game and the world moves out
	// of its way, which is what lets it slide along a wall and climb a step
	// without any of that turning into momentum.
	struct CharacterControllerComponent
	{
		// How fast it walks, in world units per second. The vertical part is
		// the game's too: the controller adds the gravity to it.
		glm::vec3 Velocity = glm::vec3(0.0f);

		// The shape and the rules it walks by.
		CharacterShape Shape;
		CharacterSettings Settings;

		// Whether it is standing on something. The game reads it to know
		// whether to jump, and the controller uses it to know whether the
		// gravity applies.
		bool OnFloor = false;

		// Whether the gravity pulls it down. A flying character turns it
		// off.
		bool UseGravity = true;

		// How fast it is falling. The gravity changes it, and landing sets
		// it back to zero - a character that keeps the speed it had when
		// it landed jumps again by itself.
		float FallSpeed = 0.0f;
	};

	// Registers every engine component with the TypeRegistry (idempotent).
	// Called by Scene's constructor; see Components.cpp for why this is a
	// function and not static-init macros.
	void RegisterEngineComponentTypes();

}
