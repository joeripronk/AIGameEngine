#include "fgpch.h"
#include "Forge/Audio/ReverbProcessor.h"

#include <algorithm>
#include <cmath>

namespace Forge {

	namespace {

		// The original Freeverb tunings, specified at 44.1 kHz. They are scaled to
		// the actual sample rate below; the ratios between them are what give the
		// reverb its character, so scaling rather than re-tuning keeps it stable
		// across sample rates.
		constexpr int kCombTuning[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
		constexpr int kAllpassTuning[4] = { 556, 441, 341, 225 };

		// How far the right channel's combs are offset, in tuning units. Spreading
		// the channels this way decorrelates them, which produces a wide stereo
		// tail rather than a mono one panned centre.
		constexpr int kStereoSpread = 23;

		constexpr double kReferenceSampleRate = 44100.0;

		// Feedback below this and the comb never converges; at or above 1 it never
		// decays. The usable range is deliberately narrower than 0..1.
		constexpr float kMinFeedback = 0.7f;
		constexpr float kMaxFeedback = 0.98f;

		// One-pole damping coefficient. Higher means more high-frequency loss.
		constexpr float kMaxDampening = 0.4f;

		float Clamp(const float value, const float low, const float high)
		{
			return value < low ? low : (value > high ? high : value);
		}

	} // namespace

	ReverbSettings ReverbProcessor::Sanitize(const ReverbSettings& settings)
	{
		ReverbSettings out = settings;

		out.RoomSize = Clamp(out.RoomSize, 0.0f, 1.0f);
		out.Dampening = Clamp(out.Dampening, 0.0f, 1.0f);
		out.Width = Clamp(out.Width, 0.0f, 1.0f);

		// Levels are clamped to [0,1]; the output stage normalises their sum so
		// wet+dry can never exceed unity.
		out.WetLevel = Clamp(out.WetLevel, 0.0f, 1.0f);
		out.DryLevel = Clamp(out.DryLevel, 0.0f, 1.0f);

		return out;
	}

	ReverbProcessor::ReverbProcessor(const ReverbSettings& settings, uint32_t sampleRate)
		: m_Settings(Sanitize(settings))
		, m_SampleRate(sampleRate == 0 ? 44100 : sampleRate)
	{
		// Dampening is applied as two cascaded one-pole coefficients; the split
		// is how Freeverb derives a single control from two filters.
		const float dampening = m_Settings.Dampening * kMaxDampening;

		m_Left.Initialize(m_SampleRate, m_Settings.RoomSize, dampening, 0);
		m_Right.Initialize(m_SampleRate, m_Settings.RoomSize, dampening, kStereoSpread);
	}

	void ReverbProcessor::Reset()
	{
		// Reinitialising with the same parameters clears the delay lines, which is
		// the simplest correct way to zero the state.
		const float dampening = m_Settings.Dampening * kMaxDampening;

		m_Left.Initialize(m_SampleRate, m_Settings.RoomSize, dampening, 0);
		m_Right.Initialize(m_SampleRate, m_Settings.RoomSize, dampening, kStereoSpread);
	}

	void ReverbProcessor::Channel::Initialize(uint32_t sampleRate, float roomSize,
		float dampening, uint32_t spread)
	{
		// Feedback maps room size onto the usable range. At roomSize 1 the comb
		// feedback is high enough to ring for a long time.
		const float feedback = kMinFeedback + roomSize * (kMaxFeedback - kMinFeedback);
		const float damp1 = dampening;
		const float damp2 = 1.0f - dampening;

		const double scale = static_cast<double>(sampleRate) / kReferenceSampleRate;

		for (int i = 0; i < 8; i++)
		{
			// The spread offsets only every other comb, which widens the field
			// without completely decorrelating the two channels.
			const int tuning = kCombTuning[i] + (i & 1 ? static_cast<int>(spread) : 0);
			const auto length = static_cast<size_t>(std::max(1.0, tuning * scale));

			CombBuffers[i].assign(length, 0.0f);
			CombIndex[i] = 0;
			CombFilterstore[i] = 0.0f;
		}

		for (int i = 0; i < 4; i++)
		{
			const auto length = static_cast<size_t>(std::max(1.0, kAllpassTuning[i] * scale));
			AllpassBuffers[i].assign(length, 0.0f);
			AllpassIndex[i] = 0;
		}

		// Retained so Process() uses the caller's parameters rather than
		// re-deriving them from defaults, which would make RoomSize and
		// Dampening inert.
		Feedback = feedback;
		Damp1 = damp1;
		Damp2 = damp2;
	}

	void ReverbProcessor::Channel::Process(float input, float& output)
	{
		float accumulator = 0.0f;

		for (int i = 0; i < 8; i++)
		{
			// The held sample is the comb's output; feedback is applied when
			// writing back into the delay line, not when reading it forward.
			// Summing the feedback into the output instead (output = delayed +
			// filterstore * feedback) makes the loop gain exceed one, and the bank
			// diverges to infinity within a second of full-scale input.
			const float delayed = CombBuffers[i][CombIndex[i]];

			// Damping one-pole: (1 - dampening) on the sample, dampening on the
			// stored value, so high frequencies decay faster in the tail.
			CombFilterstore[i] = (delayed * Damp2) + (CombFilterstore[i] * Damp1);

			CombBuffers[i][CombIndex[i]] = input + (CombFilterstore[i] * Feedback);

			accumulator += delayed;

			CombIndex[i] = (CombIndex[i] + 1) % static_cast<int>(CombBuffers[i].size());
		}

		// Averaged rather than summed: eight combs summing at unity gain would
		// multiply the signal by eight and drive the output stage into the
		// limiter for anything but the quietest input.
		accumulator *= 1.0f / 8.0f;

		// The allpass bank both decorrelates and shapes the tail.
		for (int i = 0; i < 4; i++)
		{
			const float buffer = AllpassBuffers[i][AllpassIndex[i]];
			const float result = -accumulator + buffer;
			AllpassBuffers[i][AllpassIndex[i]] = accumulator + (buffer * 0.5f);
			accumulator = result;

			AllpassIndex[i] = (AllpassIndex[i] + 1) % static_cast<int>(AllpassBuffers[i].size());
		}

		output = accumulator;
	}

	void ReverbProcessor::Process(const float* input, float* output, uint64_t frameCount)
	{
		if (!input || !output || frameCount == 0)
			return;

		// Normalise wet+dry so any legal combination of the two cannot clip.
		const float levelSum = m_Settings.WetLevel + m_Settings.DryLevel;
		const float gain = levelSum > 1.0f ? 1.0f / levelSum : 1.0f;

		const float wet = m_Settings.WetLevel * gain;
		const float dry = m_Settings.DryLevel * gain;

		// Width shapes the reverb tail only; applying it to the dry path would alter
		// the original signal, which is not what a width control should do.
		//
		// Standard formulation: wetL/wetR cross-mix the two banks. At width 0 the
		// coefficients are equal, so both outputs are the average of the tails
		// (mono); at width 1 wetR is zero, so each channel keeps its own tail
		// (natural stereo).
		const float wetL = wet * (0.5f + (0.5f * m_Settings.Width));
		const float wetR = wet * (0.5f - (0.5f * m_Settings.Width));

		// Below full width both banks are driven by the mono sum, so a source
		// panned to one side still produces a tail in both ears. Feeding a silent
		// channel with silence would leave it silent forever, since a comb fed
		// zeros stays zero.
		const bool monoExcited = m_Settings.Width < 1.0f;

		for (uint64_t frame = 0; frame < frameCount; frame++)
		{
			const float inLeft = input[frame * 2];
			const float inRight = input[(frame * 2) + 1];

			const float mono = (inLeft + inRight) * 0.5f;
			const float driveLeft = monoExcited ? mono : inLeft;
			const float driveRight = monoExcited ? mono : inRight;

			float wetOutLeft = 0.0f;
			float wetOutRight = 0.0f;
			m_Left.Process(driveLeft, wetOutLeft);
			m_Right.Process(driveRight, wetOutRight);

			float outLeft = (inLeft * dry) + ((wetOutLeft * wetL) + (wetOutRight * wetR));
			float outRight = (inRight * dry) + ((wetOutRight * wetL) + (wetOutLeft * wetR));

			// Hard limit. Eight comb filters with high feedback can sum well past
			// unity, and a limiter is preferable to letting a "reverb" clip the
			// signal it is meant to colour.
			outLeft = std::clamp(outLeft, -1.0f, 1.0f);
			outRight = std::clamp(outRight, -1.0f, 1.0f);

			output[frame * 2] = outLeft;
			output[(frame * 2) + 1] = outRight;
		}
	}

}