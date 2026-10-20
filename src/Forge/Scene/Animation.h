#pragma once

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Forge {

	class Entity;

	// What an animation animates.
	//
	// The list is the engine's, and it is short on purpose: a track names
	// what it animates, and the engine has to know how to write that value
	// onto the entity. A property that is not on this list cannot be
	// animated, and saying so is better than an animation that silently
	// does nothing.
	enum class AnimatedProperty : uint8_t
	{
		Translation = 0,   // the transform's translation (a vector)
		Rotation,          // the transform's rotation (a vector)
		Scale,             // the transform's scale (a vector)
		LightIntensity,    // a light's intensity (a number)
		Albedo,            // a material's albedo (a vector)
		Metallic,          // a material's metallic (a number)
		Roughness,         // a material's roughness (a number)
		EmissiveIntensity, // a material's emissive intensity (a number)
		AudioVolume,       // an audio source's volume (a number)
	};

	// The name a property is written under in an animation file.
	[[nodiscard]] const char* AnimatedPropertyName(AnimatedProperty property);

	// The property a name stands for. False for a name that is not one.
	[[nodiscard]] bool AnimatedPropertyFromName(const std::string& name, AnimatedProperty& out);

	// How the values between two keyframes are interpolated.
	enum class Interpolation : uint8_t
	{
		Linear,  // straight from one keyframe to the next
		Smooth,  // eased: the change starts and ends slowly
		Curve,   // the shape the author made
	};

	// A curve: the shape a value is interpolated along.
	//
	// The points are pairs, and the value between two of them is
	// interpolated. A curve shapes a track's interpolation - the same
	// "ease out" for every track that wants it, drawn once.
	struct Curve
	{
		struct Point
		{
			float Time = 0.0f;
			float Value = 0.0f;
		};

		std::vector<Point> points;

		// The value of the curve at a point.
		//
		// Before the first point the first point's value holds, and after
		// the last one the last one's. A curve with no points is the
		// straight line, which is what an author who has not drawn one
		// means.
		[[nodiscard]] float Evaluate(float time) const;
	};

	// A keyframe: a time and the value the property has then.
	//
	// A property that is a number uses x; a property that is a vector uses
	// all three. One value type for both keeps a keyframe one thing.
	struct AnimationKeyframe
	{
		float time = 0.0f;
		glm::vec3 value = glm::vec3(0.0f);
	};

	// One animated property over time.
	struct AnimationTrack
	{
		AnimatedProperty property = AnimatedProperty::Translation;
		Interpolation interpolation = Interpolation::Linear;
		std::vector<AnimationKeyframe> keys;

		// The shape the interpolation takes, when the interpolation is a
		// curve. The curve is in the space between two keyframes: 0 is the
		// first keyframe and 1 is the second.
		Curve curve;
	};

	// A clip: the tracks, and whether it loops.
	struct AnimationClip
	{
		std::string name;
		std::vector<AnimationTrack> tracks;
		bool loop = true;
	};

	namespace Animation {

		// The value of a track at a time.
		//
		// Before the first keyframe the first keyframe's value holds, and
		// after the last one the last one's: a property that is animated
		// holds its value outside the keys it has, rather than snapping to
		// zero.
		[[nodiscard]] glm::vec3 EvaluateTrack(const AnimationTrack& track, float time);

		// The length of a track: the time of its last keyframe.
		[[nodiscard]] float TrackLength(const AnimationTrack& track);

		// The length of a clip: its longest track.
		[[nodiscard]] float ClipLength(const AnimationClip& clip);

		// Writes the clip's values onto the entity. A track whose property
		// the entity does not have is skipped: an animation that moves a
		// light on an entity without a light is not an error, it is an
		// animation of nothing.
		void Apply(Entity entity, const AnimationClip& clip, float time);

	}  // namespace Animation

}  // namespace Forge
