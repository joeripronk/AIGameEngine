// Reverb DSP.
//
// The processor is pure arithmetic on PCM, so every property below is verified
// without an audio device: the tail decays, the dry signal passes through, the
// mix cannot clip, and the settings actually change the output.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include <algorithm>
#include <cmath>
#include <vector>

#include "Forge/Core/Base.h"
#include "Forge/Core/Log.h"
#include "Forge/Audio/ReverbProcessor.h"

namespace {

	constexpr uint32_t kSampleRate = 44100;

	float PeakOf(const std::vector<float>& buffer)
	{
		float peak = 0.0f;
		for (float sample : buffer)
			peak = std::max(peak, std::abs(sample));
		return peak;
	}

	float EnergyIn(const std::vector<float>& buffer, size_t fromFrame, size_t toFrame)
	{
		float total = 0.0f;
		for (size_t frame = fromFrame; frame < toFrame && frame * 2 + 1 < buffer.size(); frame++)
		{
			const float left = buffer[frame * 2];
			const float right = buffer[(frame * 2) + 1];
			total += (left * left) + (right * right);
		}
		return total;
	}

	// A single-sample impulse in both channels.
	std::vector<float> MakeImpulse(size_t frames, size_t position, float amplitude = 1.0f)
	{
		std::vector<float> buffer(frames * 2, 0.0f);
		buffer[position * 2] = amplitude;
		buffer[(position * 2) + 1] = amplitude;
		return buffer;
	}

} // namespace

TEST_CASE("Reverb settings are clamped to valid ranges", "[audio][reverb]")
{
	using Forge::ReverbSettings;
	using Forge::ReverbProcessor;

	ReverbSettings wild;
	wild.RoomSize = 5.0f;
	wild.Dampening = -3.0f;
	wild.WetLevel = 10.0f;
	wild.DryLevel = 10.0f;
	wild.Width = 99.0f;

	const ReverbSettings clean = ReverbProcessor::Sanitize(wild);

	REQUIRE(clean.RoomSize == Approx(1.0f));
	REQUIRE(clean.Dampening == Approx(0.0f));
	REQUIRE(clean.WetLevel == Approx(1.0f));
	REQUIRE(clean.DryLevel == Approx(1.0f));
	REQUIRE(clean.Width == Approx(1.0f));
}

TEST_CASE("Reverb settings are preserved when already valid", "[audio][reverb]")
{
	using Forge::ReverbSettings;
	using Forge::ReverbProcessor;

	ReverbSettings settings;
	settings.RoomSize = 0.7f;
	settings.Dampening = 0.3f;
	settings.WetLevel = 0.25f;
	settings.DryLevel = 0.75f;
	settings.Width = 0.8f;

	const ReverbSettings clean = ReverbProcessor::Sanitize(settings);

	REQUIRE(clean.RoomSize == Approx(0.7f));
	REQUIRE(clean.Dampening == Approx(0.3f));
	REQUIRE(clean.WetLevel == Approx(0.25f));
	REQUIRE(clean.DryLevel == Approx(0.75f));
	REQUIRE(clean.Width == Approx(0.8f));
}

TEST_CASE("An impulse produces a decaying tail", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	ReverbSettings settings;
	settings.RoomSize = 0.8f;
	settings.WetLevel = 1.0f;
	settings.DryLevel = 0.0f;   // isolate the wet signal

	ReverbProcessor reverb(settings, kSampleRate);

	// Long enough that the tail is clearly past its peak and into decay: a reverb
	// builds first, so a short window would still be rising at the end.
	const size_t frames = kSampleRate * 4;   // four seconds
	const auto impulse = MakeImpulse(frames, 0);

	std::vector<float> output(frames * 2, 0.0f);
	reverb.Process(impulse.data(), output.data(), frames);

	// The impulse itself is not reproduced: the comb delay lines shift it.
	REQUIRE(std::abs(output[0]) < 0.5f);

	// Something must come out; otherwise this is a silent filter.
	const float peak = PeakOf(output);
	REQUIRE(peak > 0.0001f);

	// And it must decay. Compare the second quarter (past the build-up) with the
	// last quarter; the tail has to be dying, not still swelling.
	const float settled = EnergyIn(output, frames / 4, frames / 2);
	const float late = EnergyIn(output, (frames * 3) / 4, frames);

	REQUIRE(late < settled);
}

TEST_CASE("Reverb tail is longer in a larger room", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	const size_t frames = kSampleRate * 2;

	// Measure how much energy survives a fixed time after the impulse; a larger
	// room sustains more.
	const auto tailEnergyFor = [&frames](float roomSize)
	{
		ReverbSettings settings;
		settings.RoomSize = roomSize;
		settings.WetLevel = 1.0f;
		settings.DryLevel = 0.0f;

		ReverbProcessor reverb(settings, kSampleRate);

		const auto impulse = MakeImpulse(frames, 0);
		std::vector<float> output(frames * 2, 0.0f);
		reverb.Process(impulse.data(), output.data(), frames);

		// Energy in the last quarter of a two-second buffer.
		return EnergyIn(output, (frames * 3) / 4, frames);
	};

	const float small = tailEnergyFor(0.1f);
	const float large = tailEnergyFor(0.9f);

	REQUIRE(small > 0.0f);
	REQUIRE(large > small);
}

TEST_CASE("Reverb is deterministic for identical input", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	ReverbSettings settings;
	settings.RoomSize = 0.6f;

	ReverbProcessor first(settings, kSampleRate);
	ReverbProcessor second(settings, kSampleRate);

	const size_t frames = 4096;
	std::vector<float> input(frames * 2);
	for (size_t i = 0; i < input.size(); i++)
		input[i] = std::sin(static_cast<float>(i) * 0.01f) * 0.5f;

	std::vector<float> a(frames * 2, 0.0f);
	std::vector<float> b(frames * 2, 0.0f);

	first.Process(input.data(), a.data(), frames);
	second.Process(input.data(), b.data(), frames);

	// Identical settings and input must give bit-identical output, or the effect
	// is not reproducible.
	bool identical = true;
	for (size_t i = 0; i < a.size() && identical; i++)
		identical = (a[i] == b[i]);

	REQUIRE(identical);
}

TEST_CASE("Reset clears the reverb tail", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	ReverbSettings settings;
	settings.WetLevel = 1.0f;
	settings.DryLevel = 0.0f;

	ReverbProcessor reverb(settings, kSampleRate);

	const size_t frames = kSampleRate;
	const auto impulse = MakeImpulse(frames, 0);

	std::vector<float> ringing(frames * 2, 0.0f);
	reverb.Process(impulse.data(), ringing.data(), frames);

	// Without a reset the filter state would still hold the tail.
	reverb.Reset();

	const std::vector<float> silence(frames * 2, 0.0f);
	std::vector<float> afterReset(frames * 2, 0.0f);
	reverb.Process(silence.data(), afterReset.data(), frames);

	// Silence in must give (near) silence out, or Reset did not clear anything.
	REQUIRE(PeakOf(afterReset) < 1e-6f);
}

TEST_CASE("Dry level passes the original signal through", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	ReverbSettings settings;
	settings.WetLevel = 0.0f;   // no reverb at all
	settings.DryLevel = 1.0f;

	ReverbProcessor reverb(settings, kSampleRate);

	const size_t frames = 2048;
	std::vector<float> input(frames * 2);
	for (size_t frame = 0; frame < frames; frame++)
	{
		input[frame * 2] = 0.5f;
		input[(frame * 2) + 1] = 0.5f;
	}

	std::vector<float> output(frames * 2, 0.0f);
	reverb.Process(input.data(), output.data(), frames);

	// With no wet signal the output must be the input, unchanged.
	float worst = 0.0f;
	for (size_t i = 0; i < input.size(); i++)
		worst = std::max(worst, std::abs(output[i] - input[i]));

	REQUIRE(worst <= 1e-6f);
}

TEST_CASE("Wet and dry levels cannot clip the output", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	// Wet and dry both at 1.0 sums to 2.0 before normalisation, so the output
	// stage has to scale down rather than let it clip.
	ReverbSettings settings;
	settings.WetLevel = 1.0f;
	settings.DryLevel = 1.0f;
	settings.RoomSize = 0.9f;

	ReverbProcessor reverb(settings, kSampleRate);

	const size_t frames = kSampleRate / 2;
	std::vector<float> input(frames * 2);
	for (size_t frame = 0; frame < frames; frame++)
	{
		// A loud full-scale signal, the worst case for clipping.
		input[frame * 2] = 0.999f;
		input[(frame * 2) + 1] = 0.999f;
	}

	std::vector<float> output(frames * 2, 0.0f);
	reverb.Process(input.data(), output.data(), frames);

	// Aggregate rather than one assertion per sample: a per-sample REQUIRE over
	// tens of thousands of frames buries the suite in assertion count without
	// adding any information.
	float peak = 0.0f;
	bool sawNonFinite = false;
	for (float sample : output)
	{
		if (!std::isfinite(sample))
			sawNonFinite = true;
		peak = std::max(peak, std::abs(sample));
	}

	REQUIRE_FALSE(sawNonFinite);
	REQUIRE(peak <= 1.0f + 1e-4f);
}

TEST_CASE("Reverb output is finite for extreme input", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	// Maximum feedback with maximum room size is the worst case for runaway
	// feedback in a comb bank.
	ReverbSettings settings;
	settings.RoomSize = 1.0f;
	settings.Dampening = 0.0f;
	settings.WetLevel = 1.0f;
	settings.DryLevel = 1.0f;

	ReverbProcessor reverb(settings, kSampleRate);

	const size_t frames = kSampleRate;
	const auto impulse = MakeImpulse(frames, 0, 1.0f);

	std::vector<float> output(frames * 2, 0.0f);
	reverb.Process(impulse.data(), output.data(), frames);

	bool sawNonFinite = false;
	for (float sample : output)
	{
		if (!std::isfinite(sample))
			sawNonFinite = true;
	}

	// Maximum feedback with full-scale input over seconds of audio is the worst
	// case for a recursive filter bank; it must stay finite throughout.
	REQUIRE_FALSE(sawNonFinite);
}

TEST_CASE("Reverb works in place", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	ReverbSettings settings;
	settings.RoomSize = 0.5f;

	ReverbProcessor a(settings, kSampleRate);
	ReverbProcessor b(settings, kSampleRate);

	const size_t frames = 4096;
	std::vector<float> shared(frames * 2);
	std::vector<float> separate(frames * 2);
	for (size_t frame = 0; frame < frames; frame++)
	{
		const float value = std::sin(static_cast<float>(frame) * 0.02f) * 0.5f;
		shared[frame * 2] = value;
		shared[(frame * 2) + 1] = value;
		separate = shared;
	}

	std::vector<float> expected(frames * 2, 0.0f);
	a.Process(separate.data(), expected.data(), frames);

	// The header documents that input and output may alias, so it must produce
	// the same result as separate buffers.
	b.Process(shared.data(), shared.data(), frames);

	float worst = 0.0f;
	for (size_t i = 0; i < shared.size(); i++)
		worst = std::max(worst, std::abs(shared[i] - expected[i]));

	REQUIRE(worst <= 1e-5f);
}

TEST_CASE("Reverb handles zero frames and null buffers safely", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	ReverbProcessor reverb(ReverbSettings{}, kSampleRate);

	REQUIRE_NOTHROW(reverb.Process(nullptr, nullptr, 0));

	std::vector<float> buffer(64, 0.0f);
	REQUIRE_NOTHROW(reverb.Process(buffer.data(), buffer.data(), 0));
	REQUIRE_NOTHROW(reverb.Process(nullptr, buffer.data(), 16));
	REQUIRE_NOTHROW(reverb.Process(buffer.data(), nullptr, 16));

	// A zero sample rate must not divide by zero or crash.
	REQUIRE_NOTHROW(ReverbProcessor(ReverbSettings{}, 0));
}

TEST_CASE("Room size actually changes the output", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	const size_t frames = kSampleRate;
	const auto impulse = MakeImpulse(frames, 0);

	// Peak is useless here: all eight comb filters fire together on the impulse,
	// so the peak is the attack and is the same for every room size. The tail is
	// what room size actually controls.
	const auto tailEnergyFor = [&frames, &impulse](float roomSize)
	{
		ReverbSettings settings;
		settings.RoomSize = roomSize;
		settings.WetLevel = 1.0f;
		settings.DryLevel = 0.0f;

		ReverbProcessor reverb(settings, kSampleRate);
		std::vector<float> output(frames * 2, 0.0f);
		reverb.Process(impulse.data(), output.data(), frames);
		return EnergyIn(output, frames / 2, frames);
	};

	// If the parameter were ignored (as it was once), these would be identical.
	const float small = tailEnergyFor(0.1f);
	const float large = tailEnergyFor(0.9f);

	REQUIRE(small > 0.0f);
	REQUIRE(large > small);
}

TEST_CASE("Dampening changes the output", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	const size_t frames = kSampleRate;
	const auto impulse = MakeImpulse(frames, 0);

	// As with room size, the peak is the attack; damping shows up in the tail.
	const auto tailEnergyFor = [&frames, &impulse](float dampening)
	{
		ReverbSettings settings;
		settings.Dampening = dampening;
		settings.WetLevel = 1.0f;
		settings.DryLevel = 0.0f;

		ReverbProcessor reverb(settings, kSampleRate);
		std::vector<float> output(frames * 2, 0.0f);
		reverb.Process(impulse.data(), output.data(), frames);
		return EnergyIn(output, frames / 2, frames);
	};

	// Dampening must reach the filters, not be discarded on construction.
	const float bright = tailEnergyFor(0.0f);
	const float dark = tailEnergyFor(1.0f);

	REQUIRE(bright > 0.0f);
	REQUIRE(bright != dark);
}

TEST_CASE("Width controls stereo separation", "[audio][reverb]")
{
	using Forge::ReverbProcessor;
	using Forge::ReverbSettings;

	ReverbSettings settings;
	settings.WetLevel = 1.0f;
	settings.DryLevel = 0.0f;
	settings.Width = 0.0f;   // fully collapsed to mono

	ReverbProcessor reverb(settings, kSampleRate);

	const size_t frames = kSampleRate / 4;
	std::vector<float> input(frames * 2, 0.0f);
	input[0] = 1.0f;   // impulse in the left channel only

	std::vector<float> output(frames * 2, 0.0f);
	reverb.Process(input.data(), output.data(), frames);

	// Let the tail build so a difference is measurable.
	size_t frame = 8000;
	while (frame < frames && std::abs(output[(frame * 2) + 1]) < 1e-5f)
		++frame;

	REQUIRE(frame < frames);

	// At zero width the two channels must carry the same signal, so the source
	// can no longer be localised by ear.
	const float left = output[frame * 2];
	const float right = output[(frame * 2) + 1];
	REQUIRE(left == Approx(right).margin(1e-5f));
}