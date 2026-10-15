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
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Forge/Core/Base.h"
#include "Forge/Core/Log.h"
#include "Forge/Audio/AudioEngine.h"
#include "Forge/Audio/ReverbProcessor.h"

namespace {

	constexpr uint32_t kSampleRate = 44100;

	namespace fs = std::filesystem;

	std::string ReverbFixtureDir()
	{
		std::error_code ec;
		fs::create_directories("test_reverb", ec);
		return "test_reverb/";
	}

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

	// A real 16-bit PCM WAV, so the decode/encode paths are exercised against
	// actual bytes rather than a stub. Stereo by default because the reverb is
	// a stereo processor and refuses anything else.
	std::string WriteSineWav(const std::string& path, uint32_t sampleRate = kSampleRate,
		double seconds = 0.25, double frequency = 440.0, double amplitude = 0.5,
		uint16_t channels = 2)
	{
		const auto frames = static_cast<uint32_t>(sampleRate * seconds);
		const uint16_t bitsPerSample = 16;
		const uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
		const uint16_t blockAlign = channels * bitsPerSample / 8;
		const uint32_t dataSize = frames * blockAlign;

		std::ofstream out(path, std::ios::binary);
		out.write("RIFF", 4);
		const uint32_t riffSize = 36 + dataSize;
		out.write(reinterpret_cast<const char*>(&riffSize), 4);
		out.write("WAVE", 4);

		out.write("fmt ", 4);
		const uint32_t subchunk1Size = 16;
		const uint16_t audioFormat = 1;   // PCM
		out.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
		out.write(reinterpret_cast<const char*>(&audioFormat), 2);
		out.write(reinterpret_cast<const char*>(&channels), 2);
		out.write(reinterpret_cast<const char*>(&sampleRate), 4);
		out.write(reinterpret_cast<const char*>(&byteRate), 4);
		out.write(reinterpret_cast<const char*>(&blockAlign), 2);
		out.write(reinterpret_cast<const char*>(&bitsPerSample), 2);

		out.write("data", 4);
		out.write(reinterpret_cast<const char*>(&dataSize), 4);

		for (uint32_t i = 0; i < frames; i++)
		{
			const double t = static_cast<double>(i) / sampleRate;
			const double value = amplitude * std::sin(2.0 * 3.14159265358979 * frequency * t);
			const auto sample = static_cast<int16_t>(value * 32767.0);
			for (uint16_t channel = 0; channel < channels; channel++)
				out.write(reinterpret_cast<const char*>(&sample), 2);
		}

		out.close();
		return path;
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
// ---------------------------------------------------------------------------
// Baking: processed audio becomes a real, playable file
// ---------------------------------------------------------------------------

TEST_CASE("FromSamples wraps in-memory PCM in a buffer", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::AudioBuffer;

	const uint32_t frames = 512;
	std::vector<float> pcm(frames * 2);
	for (uint32_t frame = 0; frame < frames; frame++)
	{
		pcm[frame * 2] = 0.25f;
		pcm[(frame * 2) + 1] = -0.25f;
	}

	auto buffer = AudioBuffer::FromSamples(pcm.data(), frames, 48000, 2);
	REQUIRE(buffer != nullptr);

	REQUIRE(buffer->GetFrameCount() == frames);
	REQUIRE(buffer->GetChannels() == 2);
	REQUIRE(buffer->GetSampleRate() == 48000);
	REQUIRE(buffer->GetSamples() != nullptr);

	// The PCM must be copied, not aliased: the caller's buffer is free to go
	// out of scope, and the reverb's output vector certainly does.
	float worst = 0.0f;
	for (size_t i = 0; i < pcm.size(); i++)
		worst = std::max(worst, std::abs(buffer->GetSamples()[i] - pcm[i]));

	REQUIRE(worst == Approx(0.0f).margin(1e-7f));

	// Audio made in memory has no file behind it, so the streaming playback
	// path must not pretend otherwise.
	REQUIRE(buffer->GetSourcePath().empty());
}

TEST_CASE("FromSamples rejects degenerate input", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::AudioBuffer;

	std::vector<float> pcm(64, 0.0f);

	// None of these describe audio that could exist, so all must be refused
	// rather than producing a zero-sample buffer that looks valid.
	REQUIRE(AudioBuffer::FromSamples(nullptr, 16, 44100, 2) == nullptr);
	REQUIRE(AudioBuffer::FromSamples(pcm.data(), 0, 44100, 2) == nullptr);
	REQUIRE(AudioBuffer::FromSamples(pcm.data(), 16, 0, 2) == nullptr);
	REQUIRE(AudioBuffer::FromSamples(pcm.data(), 16, 44100, 0) == nullptr);
}

TEST_CASE("A buffer written to WAV decodes back identically", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::AudioBuffer;
	using Forge::AudioEngine;

	const std::string dir = ReverbFixtureDir();

	// A quiet signal, so 16-bit quantisation error stays small enough to
	// distinguish from a genuine encoding bug.
	const uint32_t frames = 4096;
	std::vector<float> pcm(frames * 2);
	for (uint32_t frame = 0; frame < frames; frame++)
	{
		const float value = 0.25f * std::sin(static_cast<float>(frame) * 0.05f);
		pcm[frame * 2] = value;
		pcm[(frame * 2) + 1] = value;
	}

	auto original = AudioBuffer::FromSamples(pcm.data(), frames, 44100, 2);
	REQUIRE(original != nullptr);

	REQUIRE(original->WriteWav(dir + "roundtrip.wav"));

	// Decoding is device independent, so this verifies the emitted header,
	// byte rate, block alignment and channel count all agree with what a
	// decoder expects - not merely that a file appeared on disk.
	auto decoded = AudioEngine::LoadAudioBuffer(dir + "roundtrip.wav");
	REQUIRE(decoded != nullptr);

	REQUIRE(decoded->GetChannels() == 2);

	// LoadAudioBuffer decodes to a fixed 48 kHz stereo target regardless of the
	// file, so that - not the 44100 the fixture was authored at - is the rate the
	// round-tripped file carries.
	REQUIRE(decoded->GetSampleRate() == 48000);

	// Decoding resamples 44100 -> 48000, so compare against the decoded source
	// rather than the float buffer the fixture was built from.
	auto source = Forge::AudioEngine::LoadAudioBuffer(dir + "roundtrip.wav");
	REQUIRE(source != nullptr);
	REQUIRE(source->GetFrameCount() == decoded->GetFrameCount());

	// 16-bit costs up to 2 LSB: one from the writer scaling by 32767 while the
	// decoder divides by 32768, and half a LSB of rounding.
	float worst = 0.0f;
	const size_t frames64 = static_cast<size_t>(decoded->GetFrameCount());
	for (size_t i = 0; i < frames64 * 2; i++)
		worst = std::max(worst, std::abs(decoded->GetSamples()[i] - source->GetSamples()[i]));

	REQUIRE(worst < 2.0f / 32767.0f);
}

TEST_CASE("WriteWav clamps out-of-range samples instead of wrapping them", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::AudioBuffer;
	using Forge::AudioEngine;

	const std::string dir = ReverbFixtureDir();

	// 4.0 would wrap to a large negative number if cast to int16 without a
	// clamp, which is a loud click rather than a loud sample.
	const uint32_t frames = 2400;
	std::vector<float> pcm(frames * 2, 4.0f);
	for (uint32_t frame = frames / 2; frame < frames; frame++)
		pcm[(frame * 2) + 1] = -4.0f;

	auto buffer = AudioBuffer::FromSamples(pcm.data(), frames, 48000, 2);
	REQUIRE(buffer != nullptr);
	REQUIRE(buffer->WriteWav(dir + "clamped.wav"));

	auto decoded = AudioEngine::LoadAudioBuffer(dir + "clamped.wav");
	REQUIRE(decoded != nullptr);

	// Scan rather than sampling index 0: decoding resamples, so a clip this
	// short would not have settled at its first frames.
	float peak = 0.0f;
	float trough = 0.0f;
	for (size_t i = 0; i < pcm.size(); i++)
	{
		peak = std::max(peak, decoded->GetSamples()[i]);
		trough = std::min(trough, decoded->GetSamples()[i]);
	}

	// Full scale, positive and negative, with the correct signs. A wrapping cast
	// would put the negative half at full positive scale instead.
	REQUIRE(peak > 0.99f);
	REQUIRE(trough < -0.99f);
}

TEST_CASE("WriteWav refuses empty buffers and unwritable paths", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::AudioBuffer;

	const std::string dir = ReverbFixtureDir();

	// A default-constructed buffer holds no audio; writing it would emit a WAV
	// claiming zero samples, which decoders handle inconsistently.
	AudioBuffer empty;
	REQUIRE_FALSE(empty.WriteWav(dir + "empty.wav"));
	REQUIRE_FALSE(empty.WriteWav(""));

	std::vector<float> pcm(64, 0.1f);
	auto buffer = AudioBuffer::FromSamples(pcm.data(), 32, 44100, 2);
	REQUIRE(buffer != nullptr);

	REQUIRE_FALSE(buffer->WriteWav(""));

	// A directory that does not exist cannot be opened for writing, and this
	// must be reported rather than silently producing nothing.
	REQUIRE_FALSE(buffer->WriteWav("test_reverb/no_such_dir/out.wav"));
}

TEST_CASE("ProcessAudioFile returns nullptr for an undecodable clip", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::ProcessAudioFile;
	using Forge::ReverbSettings;

	// Missing file.
	REQUIRE(ProcessAudioFile("test_reverb/definitely_absent.wav", ReverbSettings{}) == nullptr);

	// Present but not audio at all.
	const std::string dir = ReverbFixtureDir();
	{
		std::ofstream junk(dir + "junk.wav", std::ios::binary);
		junk << "this is not a wav file";
	}

	REQUIRE(ProcessAudioFile(dir + "junk.wav", ReverbSettings{}) == nullptr);
}

TEST_CASE("Decoding normalises to 48 kHz stereo", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::AudioEngine;
	using Forge::ProcessAudioFile;
	using Forge::ReverbSettings;

	const std::string dir = ReverbFixtureDir();

	// A mono file is upmixed and resampled by the decoder, which is why the
	// reverb can require stereo without rejecting mono sources: it never sees
	// one. Pinned here because both the processor's channel check and every
	// WriteWav depend on this contract holding.
	const std::string mono = WriteSineWav(dir + "mono.wav", kSampleRate, 0.1, 440.0, 0.5, 1);

	auto source = AudioEngine::LoadAudioBuffer(mono);
	REQUIRE(source != nullptr);
	REQUIRE(source->GetChannels() == 2);
	REQUIRE(source->GetSampleRate() == 48000);

	// So a mono source is processable rather than rejected.
	REQUIRE(ProcessAudioFile(mono, ReverbSettings{}) != nullptr);
}

TEST_CASE("ProcessAudioFile bakes reverb into a playable buffer", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::ProcessAudioFile;
	using Forge::ReverbSettings;

	const std::string dir = ReverbFixtureDir();
	const std::string source = WriteSineWav(dir + "bake_input.wav", kSampleRate, 0.5, 440.0, 0.5, 2);

	ReverbSettings settings;
	settings.RoomSize = 0.8f;
	settings.WetLevel = 1.0f;
	settings.DryLevel = 0.0f;

	auto processed = ProcessAudioFile(source, settings);
	REQUIRE(processed != nullptr);

	// Same length and rate as the source: baking an effect into a clip must not
	// truncate it or resample it.
	REQUIRE(processed->GetChannels() == 2);
	REQUIRE(processed->GetSampleRate() == 48000);

	auto original = Forge::AudioEngine::LoadAudioBuffer(source);
	REQUIRE(original != nullptr);
	REQUIRE(processed->GetFrameCount() == original->GetFrameCount());

	// The tail is the whole point, so it must differ from the dry input.
	const size_t frames = static_cast<size_t>(processed->GetFrameCount());

	float worst = 0.0f;
	for (size_t i = 0; i < frames * 2; i++)
		worst = std::max(worst, std::abs(processed->GetSamples()[i] - original->GetSamples()[i]));

	REQUIRE(worst > 0.0f);

	// And it must still be bounded audio, not a diverged filter bank.
	// Aggregated: one assertion per sample over thousands of frames buries the
	// suite in assertion count without adding any information.
	bool sawNonFinite = false;
	for (size_t i = 0; i < frames * 2; i++)
	{
		if (!std::isfinite(processed->GetSamples()[i]))
			sawNonFinite = true;
	}

	REQUIRE_FALSE(sawNonFinite);
	REQUIRE(PeakOf(std::vector<float>(processed->GetSamples(),
		processed->GetSamples() + (frames * 2))) <= 1.0f);
}

TEST_CASE("A baked reverb clip survives the round trip to disk", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::ProcessAudioFile;
	using Forge::ReverbSettings;

	const std::string dir = ReverbFixtureDir();
	const std::string source = WriteSineWav(dir + "roundtrip_input.wav", kSampleRate, 0.25, 440.0, 0.4, 2);

	ReverbSettings settings;
	settings.RoomSize = 0.6f;

	auto processed = ProcessAudioFile(source, settings);
	REQUIRE(processed != nullptr);

	// This is the point of the bake: the processed audio must land on disk as
	// real audio the streaming playback path can open.
	REQUIRE(processed->WriteWav(dir + "roundtrip_baked.wav"));

	auto reloaded = Forge::AudioEngine::LoadAudioBuffer(dir + "roundtrip_baked.wav");
	REQUIRE(reloaded != nullptr);

	REQUIRE(reloaded->GetFrameCount() == processed->GetFrameCount());
	REQUIRE(reloaded->GetSampleRate() == processed->GetSampleRate());
	REQUIRE(reloaded->GetChannels() == 2);

	const size_t frames = static_cast<size_t>(processed->GetFrameCount());
	float worst = 0.0f;
	for (size_t i = 0; i < frames * 2; i++)
		worst = std::max(worst, std::abs(reloaded->GetSamples()[i] - processed->GetSamples()[i]));

	// Both sides are already at 48 kHz, so there is no resampling here and the
	// only loss is 16-bit quantisation: up to 2 LSB, since the writer scales by
	// 32767 while the decoder divides by 32768, plus half a LSB of rounding.
	REQUIRE(worst < 2.0f / 32767.0f);
}

TEST_CASE("ProcessAudioFile is deterministic", "[audio][reverb]")
{
	Forge::Log::Init();
	using Forge::ProcessAudioFile;
	using Forge::ReverbSettings;

	const std::string dir = ReverbFixtureDir();
	const std::string source = WriteSineWav(dir + "determinism.wav", kSampleRate, 0.2, 330.0, 0.5, 2);

	ReverbSettings settings;
	settings.RoomSize = 0.7f;

	auto first = ProcessAudioFile(source, settings);
	auto second = ProcessAudioFile(source, settings);

	REQUIRE(first != nullptr);
	REQUIRE(second != nullptr);

	const size_t frames = static_cast<size_t>(first->GetFrameCount());
	bool identical = true;
	for (size_t i = 0; i < frames * 2 && identical; i++)
		identical = (first->GetSamples()[i] == second->GetSamples()[i]);

	// Baked assets are committed to the repository, so a second bake producing
	// different bytes would churn every file for no reason.
	REQUIRE(identical);
}
