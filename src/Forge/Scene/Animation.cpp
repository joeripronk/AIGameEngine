#include "fgpch.h"
#include "Forge/Scene/Animation.h"

#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"

#include <algorithm>
#include <cmath>

namespace Forge {

	namespace {

		struct NamedProperty
		{
			const char* name;
			AnimatedProperty property;
		};

		// The names the properties are written under in an animation file.
		// The list is the engine's, and it is the same list the property
		// enum is: a name that is not in it is not a property.
		const NamedProperty kNamedProperties[] = {
			{ "Translation", AnimatedProperty::Translation },
			{ "Rotation", AnimatedProperty::Rotation },
			{ "Scale", AnimatedProperty::Scale },
			{ "LightIntensity", AnimatedProperty::LightIntensity },
			{ "Albedo", AnimatedProperty::Albedo },
			{ "Metallic", AnimatedProperty::Metallic },
			{ "Roughness", AnimatedProperty::Roughness },
			{ "EmissiveIntensity", AnimatedProperty::EmissiveIntensity },
			{ "AudioVolume", AnimatedProperty::AudioVolume },
		};

	}  // namespace

	const char* AnimatedPropertyName(AnimatedProperty property)
	{
		for (const NamedProperty& named : kNamedProperties)
		{
			if (named.property == property)
				return named.name;
		}
		return "Translation";
	}

	bool AnimatedPropertyFromName(const std::string& name, AnimatedProperty& out)
	{
		for (const NamedProperty& named : kNamedProperties)
		{
			if (name == named.name)
			{
				out = named.property;
				return true;
			}
		}
		return false;
	}

	float Curve::Evaluate(float time) const
	{
		// A curve with no points is the straight line.
		if (points.empty())
			return time;

		if (time <= points.front().Time)
			return points.front().Value;
		if (time >= points.back().Time)
			return points.back().Value;

		// The points around the time, in the curve's own order.
		size_t next = 1;
		while (next < points.size() && points[next].Time <= time)
			++next;

		const Point& from = points[next - 1];
		const Point& to = points[next];

		const float span = to.Time - from.Time;
		if (span <= 0.0f)
			return to.Value;

		const float alpha = (time - from.Time) / span;
		return from.Value + (to.Value - from.Value) * alpha;
	}

	namespace Animation {

		glm::vec3 EvaluateTrack(const AnimationTrack& track, float time)
		{
			if (track.keys.empty())
				return glm::vec3(0.0f);

			// Before the first keyframe the first keyframe's value holds, and
			// after the last one the last one's. A property that is animated
			// holds its value outside the keys it has, rather than snapping
			// to zero - an animation that moves a light from 0 to 1 and back
			// must not turn the light off between the keys.
			if (time <= track.keys.front().time)
				return track.keys.front().value;
			if (time >= track.keys.back().time)
				return track.keys.back().value;

			// The keyframes around the time. The keys are sorted by time, so
			// the one before it is the last one that is not after it.
			size_t next = 1;
			while (next < track.keys.size() && track.keys[next].time <= time)
				++next;

			const AnimationKeyframe& from = track.keys[next - 1];
			const AnimationKeyframe& to = track.keys[next];

			const float span = to.time - from.time;
			if (span <= 0.0f)
				return to.value;

			float alpha = (time - from.time) / span;
			if (track.interpolation == Interpolation::Smooth)
			{
				// Smoothstep: the change starts and ends slowly, which is
				// what a movement that has to look like it has weight does.
				alpha = alpha * alpha * (3.0f - 2.0f * alpha);
			}
			else if (track.interpolation == Interpolation::Curve)
			{
				// The author's shape: the same "ease out" for every track
				// that wants it, drawn once.
				alpha = track.curve.Evaluate(alpha);
			}

			return from.value + (to.value - from.value) * alpha;
		}

		float TrackLength(const AnimationTrack& track)
		{
			if (track.keys.empty())
				return 0.0f;
			return track.keys.back().time;
		}

		float ClipLength(const AnimationClip& clip)
		{
			float length = 0.0f;
			for (const AnimationTrack& track : clip.tracks)
				length = std::max(length, TrackLength(track));
			return length;
		}

		void Apply(Entity entity, const AnimationClip& clip, float time)
		{
			if (!entity)
				return;

			for (const AnimationTrack& track : clip.tracks)
			{
				const glm::vec3 value = EvaluateTrack(track, time);

				switch (track.property)
				{
				case AnimatedProperty::Translation:
					if (entity.HasComponent<TransformComponent>())
						entity.GetComponent<TransformComponent>().Translation = value;
					break;

				case AnimatedProperty::Rotation:
					if (entity.HasComponent<TransformComponent>())
						entity.GetComponent<TransformComponent>().Rotation = value;
					break;

				case AnimatedProperty::Scale:
					if (entity.HasComponent<TransformComponent>())
						entity.GetComponent<TransformComponent>().Scale = value;
					break;

				case AnimatedProperty::LightIntensity:
					if (entity.HasComponent<LightComponent>())
						entity.GetComponent<LightComponent>().Intensity = value.x;
					break;

				case AnimatedProperty::Albedo:
					if (entity.HasComponent<PBRMaterialComponent>())
						entity.GetComponent<PBRMaterialComponent>().Albedo = value;
					break;

				case AnimatedProperty::Metallic:
					if (entity.HasComponent<PBRMaterialComponent>())
						entity.GetComponent<PBRMaterialComponent>().Metallic = value.x;
					break;

				case AnimatedProperty::Roughness:
					if (entity.HasComponent<PBRMaterialComponent>())
						entity.GetComponent<PBRMaterialComponent>().Roughness = value.x;
					break;

				case AnimatedProperty::EmissiveIntensity:
					if (entity.HasComponent<PBRMaterialComponent>())
						entity.GetComponent<PBRMaterialComponent>().EmissiveIntensity = value.x;
					break;

				case AnimatedProperty::AudioVolume:
					if (entity.HasComponent<AudioSourceComponent>())
						entity.GetComponent<AudioSourceComponent>().Volume = value.x;
					break;
				}
			}
		}

	}  // namespace Animation

}  // namespace Forge
