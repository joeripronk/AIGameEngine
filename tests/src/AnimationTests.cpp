#include <catch2/catch_test_macros.hpp>

#include "Forge/Scene/Animation.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/SceneSerializer.h"

using namespace Forge;

namespace {

	// A track with two keys: 0 at the start, 10 at the end.
	AnimationTrack MakeTrack(AnimatedProperty property = AnimatedProperty::Translation)
	{
		AnimationTrack track;
		track.property = property;
		track.keys.push_back({ 0.0f, glm::vec3(0.0f) });
		track.keys.push_back({ 1.0f, glm::vec3(10.0f) });
		return track;
	}

}  // namespace

TEST_CASE("A track is interpolated between its keyframes", "[scene][animation]")
{
	const AnimationTrack track = MakeTrack();

	// At the keyframes the value is the keyframe's.
	REQUIRE(Animation::EvaluateTrack(track, 0.0f) == glm::vec3(0.0f));
	REQUIRE(Animation::EvaluateTrack(track, 1.0f) == glm::vec3(10.0f));

	// In between, the value is between them.
	REQUIRE(Animation::EvaluateTrack(track, 0.5f) == glm::vec3(5.0f));
	REQUIRE(Animation::EvaluateTrack(track, 0.25f) == glm::vec3(2.5f));

	// Before the first keyframe and after the last one, the value holds: a
	// property that is animated holds its value outside the keys it has,
	// rather than snapping to zero.
	REQUIRE(Animation::EvaluateTrack(track, -1.0f) == glm::vec3(0.0f));
	REQUIRE(Animation::EvaluateTrack(track, 5.0f) == glm::vec3(10.0f));

	// A track with no keys has no value.
	AnimationTrack empty;
	REQUIRE(Animation::EvaluateTrack(empty, 0.0f) == glm::vec3(0.0f));
	REQUIRE(Animation::TrackLength(empty) == 0.0f);
}

TEST_CASE("A smooth track eases between its keyframes", "[scene][animation]")
{
	AnimationTrack track = MakeTrack();
	track.interpolation = Interpolation::Smooth;

	// The midpoint of a smooth track is the midpoint of a linear one, but
	// the quarters are not: the change starts and ends slowly.
	REQUIRE(Animation::EvaluateTrack(track, 0.5f) == glm::vec3(5.0f));
	REQUIRE(Animation::EvaluateTrack(track, 0.25f).x < 2.5f);
	REQUIRE(Animation::EvaluateTrack(track, 0.75f).x > 7.5f);
}

TEST_CASE("A clip is as long as its longest track", "[scene][animation]")
{
	AnimationClip clip;
	clip.tracks.push_back(MakeTrack());

	AnimationTrack longer = MakeTrack();
	longer.keys.back().time = 3.0f;
	clip.tracks.push_back(longer);

	REQUIRE(Animation::ClipLength(clip) == 3.0f);
}

TEST_CASE("The properties are named both ways", "[scene][animation]")
{
	REQUIRE(std::string(AnimatedPropertyName(AnimatedProperty::Translation)) == "Translation");
	REQUIRE(std::string(AnimatedPropertyName(AnimatedProperty::LightIntensity)) == "LightIntensity");

	AnimatedProperty property = AnimatedProperty::Scale;
	REQUIRE(AnimatedPropertyFromName("Scale", property));
	REQUIRE(property == AnimatedProperty::Scale);

	// A name that is not a property is not a property.
	REQUIRE_FALSE(AnimatedPropertyFromName("NotAProperty", property));
}

TEST_CASE("An animation writes its values onto the entity", "[scene][animation]")
{
	Scene scene;
	Entity entity = scene.CreateEntity("Animated");
	entity.AddComponent<LightComponent>();
	entity.AddComponent<PBRMaterialComponent>();

	AnimationClip clip;
	clip.tracks.push_back(MakeTrack(AnimatedProperty::Translation));

	AnimationTrack light = MakeTrack(AnimatedProperty::LightIntensity);
	light.keys[0].value = glm::vec3(0.0f);
	light.keys[1].value = glm::vec3(4.0f);
	clip.tracks.push_back(light);

	AnimationTrack albedo = MakeTrack(AnimatedProperty::Albedo);
	albedo.keys[0].value = glm::vec3(0.0f);
	albedo.keys[1].value = glm::vec3(1.0f);
	clip.tracks.push_back(albedo);

	Animation::Apply(entity, clip, 0.5f);

	REQUIRE(entity.GetComponent<TransformComponent>().Translation == glm::vec3(5.0f));
	REQUIRE(entity.GetComponent<LightComponent>().Intensity == 2.0f);
	REQUIRE(entity.GetComponent<PBRMaterialComponent>().Albedo == glm::vec3(0.5f));

	// A track whose property the entity does not have is skipped: an
	// animation of a light on an entity without a light is not an error.
	Entity bare = scene.CreateEntity("Bare");
	Animation::Apply(bare, clip, 0.5f);
	REQUIRE(bare.GetComponent<TransformComponent>().Translation == glm::vec3(5.0f));
}

TEST_CASE("The scene advances the animations and loops them", "[scene][animation]")
{
	Scene scene;
	Entity entity = scene.CreateEntity("Animated");

	AnimationClip clip;
	clip.tracks.push_back(MakeTrack());
	clip.loop = true;

	auto& animation = entity.AddComponent<AnimationComponent>(clip);

	// Half a second into a one-second clip, the value is halfway.
	scene.OnUpdateRuntime(Timestep(0.5f));
	REQUIRE(animation.Time == 0.5f);
	REQUIRE(entity.GetComponent<TransformComponent>().Translation == glm::vec3(5.0f));

	// The clip runs out and starts again: the time it overshot by is kept.
	scene.OnUpdateRuntime(Timestep(0.75f));
	REQUIRE(animation.Time == 0.25f);
	REQUIRE(entity.GetComponent<TransformComponent>().Translation == glm::vec3(2.5f));

	// A clip that does not loop stops at its end and holds the value.
	animation.Clip.loop = false;
	animation.Time = 0.0f;
	scene.OnUpdateRuntime(Timestep(2.0f));
	REQUIRE(animation.Time == 1.0f);
	REQUIRE_FALSE(animation.Playing);
	REQUIRE(entity.GetComponent<TransformComponent>().Translation == glm::vec3(10.0f));
}

TEST_CASE("An animation survives the runtime format too", "[scene][animation]")
{
	// The runtime format is the one a shipped game loads, and its payloads
	// are a fixed layout: the animation's is a variable-length one, so the
	// counts are written with it.
	const std::string path = "test_animation.rt";

	{
		Ref<Scene> scene = CreateRef<Scene>();
		Entity entity = scene->CreateEntity("Animated");

		AnimationClip clip;
		clip.loop = false;
		clip.tracks.push_back(MakeTrack());

		AnimationTrack light = MakeTrack(AnimatedProperty::LightIntensity);
		light.interpolation = Interpolation::Smooth;
		light.keys[0].value = glm::vec3(1.0f);
		light.keys[1].value = glm::vec3(5.0f);
		clip.tracks.push_back(light);

		auto& animation = entity.AddComponent<AnimationComponent>(clip);
		animation.Time = 0.25f;
		animation.Playing = false;

		SceneSerializer serializer(scene);
		serializer.SerializeRuntime(path);
	}

	{
		Ref<Scene> scene = CreateRef<Scene>();
		SceneSerializer serializer(scene);
		REQUIRE(serializer.DeserializeRuntime(path));

		Entity entity = scene->FindEntityByName("Animated");
		REQUIRE(entity);
		REQUIRE(entity.HasComponent<AnimationComponent>());

		const AnimationComponent& animation = entity.GetComponent<AnimationComponent>();
		REQUIRE(animation.Time == 0.25f);
		REQUIRE_FALSE(animation.Playing);
		REQUIRE_FALSE(animation.Clip.loop);
		REQUIRE(animation.Clip.tracks.size() == 2);
		REQUIRE(animation.Clip.tracks[0].property == AnimatedProperty::Translation);
		REQUIRE(animation.Clip.tracks[0].keys[1].value == glm::vec3(10.0f));
		REQUIRE(animation.Clip.tracks[1].property == AnimatedProperty::LightIntensity);
		REQUIRE(animation.Clip.tracks[1].interpolation == Interpolation::Smooth);
		REQUIRE(animation.Clip.tracks[1].keys[0].value == glm::vec3(1.0f));
	}

	std::remove(path.c_str());
}

TEST_CASE("An animation survives a round trip through a scene file", "[scene][animation]")
{
	const std::string path = "test_animation.forge";

	{
		Ref<Scene> scene = CreateRef<Scene>();
		Entity entity = scene->CreateEntity("Animated");

		AnimationClip clip;
		clip.name = "Bob";
		clip.loop = false;
		clip.tracks.push_back(MakeTrack());

		AnimationTrack light = MakeTrack(AnimatedProperty::LightIntensity);
		light.interpolation = Interpolation::Smooth;
		light.keys[0].value = glm::vec3(1.0f);
		light.keys[1].value = glm::vec3(5.0f);
		clip.tracks.push_back(light);

		auto& animation = entity.AddComponent<AnimationComponent>(clip);
		animation.Time = 0.25f;
		animation.Speed = 2.0f;

		SceneSerializer serializer(scene);
		REQUIRE(serializer.Serialize(path));
	}

	{
		Ref<Scene> scene = CreateRef<Scene>();
		SceneSerializer serializer(scene);
		REQUIRE(serializer.Deserialize(path));

		Entity entity = scene->FindEntityByName("Animated");
		REQUIRE(entity);
		REQUIRE(entity.HasComponent<AnimationComponent>());

		const AnimationComponent& animation = entity.GetComponent<AnimationComponent>();
		REQUIRE(animation.Time == 0.25f);
		REQUIRE(animation.Speed == 2.0f);
		REQUIRE_FALSE(animation.Clip.loop);
		REQUIRE(animation.Clip.tracks.size() == 2);

		REQUIRE(animation.Clip.tracks[0].property == AnimatedProperty::Translation);
		REQUIRE(animation.Clip.tracks[0].keys.size() == 2);
		REQUIRE(animation.Clip.tracks[0].keys[1].value == glm::vec3(10.0f));

		REQUIRE(animation.Clip.tracks[1].property == AnimatedProperty::LightIntensity);
		REQUIRE(animation.Clip.tracks[1].interpolation == Interpolation::Smooth);
		REQUIRE(animation.Clip.tracks[1].keys[0].value == glm::vec3(1.0f));
	}

	std::remove(path.c_str());
}
