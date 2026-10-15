#pragma once

#include "Forge/Core/Base.h"

#include <cstdint>
#include <vector>

namespace Forge {

	// A simple algorithmic reverb, in the Freeverb style: a bank of feedback
	// comb filters followed by a bank of allpass filters, per channel.
	//
	// This is implemented in the engine rather than delegated to miniaudio's
	// node graph, because the vendored build exposes only `sound` and `group`
	// node types - there are no DSP nodes to attach a reverb to. The upside is
	// that it is pure arithmetic on PCM, so it is unit-testable with no audio
	// device at all.
	//
	// Processing is a separate step from playback: load a clip, run it through
	// a processor to bake in the effect, then play the result. That suits music
	// and ambience, where the tail does not need to react to anything.
	struct ReverbSettings
	{
		// 0 = small dry room, 1 = large hall. Drives comb feedback.
		float RoomSize = 0.5f;

		// 0 = bright and metallic, 1 = dark and muffled. Higher values lose
		// high frequency energy faster in the comb feedback path.
		float Dampening = 0.5f;

		// Levels of the processed (wet) and original (dry) signal. Their sum is
		// clamped to avoid clipping.
		float WetLevel = 0.33f;
		float DryLevel = 0.7f;

		// Stereo width: 0 collapses to mono, 1 is the full natural stereo image.
		float Width = 1.0f;
	};

	class ReverbProcessor
	{
	public:
		// `sampleRate` must be positive; settings are clamped to their valid
		// ranges on construction.
		ReverbProcessor(const ReverbSettings& settings, uint32_t sampleRate);

		// Clears the filter state so the next block starts from silence rather
		// than the previous clip's tail.
		void Reset();

		// Processes interleaved stereo 32-bit float PCM.
		//
		// `input` and `output` may be the same buffer for an in-place process.
		// The processor reads ahead within a block, so overlapping in/out with an
		// offset is not supported; either use distinct buffers or copy first.
		void Process(const float* input, float* output, uint64_t frameCount);

		const ReverbSettings& GetSettings() const { return m_Settings; }

		// The settings after clamping, which is what the filters actually use.
		static ReverbSettings Sanitize(const ReverbSettings& settings);

	private:
		// One channel's filter bank. Feedback comb filters followed by allpass
		// filters, which is the standard Freeverb topology.
		struct Channel
		{
			// Comb filters: store an input sample and mix it back after
			// `Filterstore` frames, attenuated by feedback.
			std::vector<float> CombBuffers[8];
			int CombIndex[8] = {};
			float CombFilterstore[8] = {};

			// Allpass filters: shift the stream by a fixed delay while preserving
			// the spectrum, which smears transients into the tail.
			std::vector<float> AllpassBuffers[4];
			int AllpassIndex[4] = {};

			// Derived from the settings by Initialize and reused per sample, so
			// the parameters are honoured rather than recomputed from defaults.
			float Feedback = 0.84f;
			float Damp1 = 0.2f;
			float Damp2 = 0.8f;

			void Initialize(uint32_t sampleRate, float roomSize, float dampening, uint32_t spread);
			void Process(float input, float& output);
		};

		ReverbSettings m_Settings;
		uint32_t m_SampleRate;
		Channel m_Left;
		Channel m_Right;
	};

}