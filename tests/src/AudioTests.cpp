// Audio: engine handles, the AudioSourceComponent, and graceful degradation.
//
// A machine with no sound card (CI, containers, headless) must still run the
// game, so these tests are written to pass whether or not an output device
// exists. What is asserted unconditionally is the behaviour that does not
// depend on one: decoding, handle lifetime rules, serialization, and the
// never-fatal contract.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

#include "Forge/Core/Base.h"
#include "Forge/Core/Log.h"
#include "Forge/Audio/AudioEngine.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/SceneSerializer.h"
#include "Forge/Scripting/ScriptEngine.h"

namespace {

	namespace fs = std::filesystem;

	std::string AudioFixtureDir()
	{
		std::error_code ec;
		fs::create_directories("test_audio", ec);
		return "test_audio/";
	}

	// Writes a real 16-bit PCM mono WAV containing a constant-amplitude sine, so
	// decoding is exercised against actual bytes rather than a stub file.
	std::string WriteSineWav(const std::string& path, int sampleRate = 48000,
		double seconds = 0.25, double frequency = 440.0, double amplitude = 0.5)
	{
		const auto frames = static_cast<uint32_t>(sampleRate * seconds);
		const uint16_t channels = 1;
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
		const uint16_t audioFormat = 1; // PCM
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
			out.write(reinterpret_cast<const char*>(&sample), 2);
		}

		out.close();
		return path;
	}

} // namespace

// ---------------------------------------------------------------------------
// AudioBuffer: decoding is device independent
// ---------------------------------------------------------------------------

TEST_CASE("AudioEngine decodes a WAV file into PCM", "[audio]")
{
	Forge::Log::Init();
	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone.wav");

	// Decoding must not require an output device, which is what makes it
	// testable in CI at all.
	auto buffer = Forge::AudioEngine::LoadAudioBuffer(wav);
	REQUIRE(buffer);
	REQUIRE(buffer->GetSampleRate() == 48000);
	REQUIRE(buffer->GetChannels() == 2);
	REQUIRE(buffer->GetFrameCount() > 0);
	REQUIRE(buffer->GetSamples() != nullptr);
	REQUIRE(buffer->GetDuration() == Approx(0.25f).margin(0.05f));
}

TEST_CASE("AudioBuffer reports its source path", "[audio]")
{
	Forge::Log::Init();
	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_path.wav");

	auto buffer = Forge::AudioEngine::LoadAudioBuffer(wav);
	REQUIRE(buffer);
	REQUIRE(buffer->GetSourcePath() == wav);
}

TEST_CASE("Decoded samples carry real signal, not silence", "[audio]")
{
	Forge::Log::Init();
	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_signal.wav");

	auto buffer = Forge::AudioEngine::LoadAudioBuffer(wav);
	REQUIRE(buffer);

	// A decoder that returned zeros would pass a "frame count > 0" check while
	// producing silence, so assert the samples actually vary.
	float peak = 0.0f;
	const float* samples = buffer->GetSamples();
	REQUIRE(samples != nullptr);
	for (uint64_t i = 0; i < buffer->GetFrameCount() * buffer->GetChannels(); i++)
		peak = std::max(peak, std::abs(samples[i]));

	REQUIRE(peak > 0.1f);
	REQUIRE(peak <= 1.0f);
}

TEST_CASE("Loading a missing audio file fails cleanly", "[audio]")
{
	Forge::Log::Init();
	REQUIRE(Forge::AudioEngine::LoadAudioBuffer("test_audio/definitely_not_here.wav") == nullptr);
}

TEST_CASE("Loading a non-audio file fails cleanly", "[audio]")
{
	Forge::Log::Init();

	std::error_code ec;
	fs::create_directories("test_audio", ec);
	{
		std::ofstream out("test_audio/not_audio.wav", std::ios::binary);
		out << "this is plain text, not a wav file";
	}

	REQUIRE(Forge::AudioEngine::LoadAudioBuffer("test_audio/not_audio.wav") == nullptr);
}

// ---------------------------------------------------------------------------
// Sound handles
// ---------------------------------------------------------------------------

TEST_CASE("Sound handles are invalid before anything plays", "[audio]")
{
	Forge::AudioEngine::Shutdown();

	REQUIRE(Forge::AudioEngine::InvalidSound == 0);
	REQUIRE(Forge::AudioEngine::IsValidSound(Forge::AudioEngine::InvalidSound) == false);
	REQUIRE(Forge::AudioEngine::IsValidSound(12345) == false);
	REQUIRE(Forge::AudioEngine::GetActiveSoundCount() == 0);
}

TEST_CASE("Sound handle operations fail safely when uninitialized", "[audio]")
{
	Forge::AudioEngine::Shutdown();

	// Audio is optional, so every call must be safe on a machine with no
	// device rather than crashing or corrupting state.
	REQUIRE(Forge::AudioEngine::StopSound(1) == false);
	REQUIRE(Forge::AudioEngine::SetSoundVolume(1, 0.5f) == false);
	REQUIRE(Forge::AudioEngine::SetSoundPitch(1, 2.0f) == false);
	REQUIRE(Forge::AudioEngine::IsSoundPlaying(1) == false);
	Forge::AudioEngine::StopAllSounds();
}

TEST_CASE("Playing with no path or no device reports failure", "[audio]")
{
	Forge::AudioEngine::Shutdown();

	Forge::AudioEngine::SoundHandle handle = Forge::AudioEngine::InvalidSound;
	REQUIRE(Forge::AudioEngine::PlaySound("", 1.0f, 1.0f, false, &handle) == false);
	REQUIRE(handle == Forge::AudioEngine::InvalidSound);

	// If a device happens to exist this may succeed; either way the handle must
	// be left in a coherent state.
	// May or may not start depending on whether a device exists; the handle must
	// be coherent either way.
	const bool started = Forge::AudioEngine::PlaySound("test_audio/tone.wav", 1.0f, 1.0f, false, &handle);
	if (!started)
		REQUIRE(handle == Forge::AudioEngine::InvalidSound);
	else
		REQUIRE(Forge::AudioEngine::IsValidSound(handle));

	Forge::AudioEngine::Shutdown();
}

TEST_CASE("Sounds are tracked individually and released on stop", "[audio]")
{
	Forge::AudioEngine::Shutdown();

	if (!Forge::AudioEngine::Init())
	{
		// No output device here; the handle bookkeeping is exercised by the
		// uninitialized tests above.
		SUCCEED("No audio device; per-sound handles require one to verify");
		return;
	}

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_handles.wav", 48000, 1.0);

	// Fades off so release is immediate and deterministic. Fade length is
	// measured in audio frames, which only advance once the device has actually
	// run, so the deferred path has its own test below.
	Forge::AudioEngine::SetFadeDuration(0.0f);

	Forge::AudioEngine::SoundHandle first = Forge::AudioEngine::InvalidSound;
	Forge::AudioEngine::SoundHandle second = Forge::AudioEngine::InvalidSound;

	REQUIRE(Forge::AudioEngine::PlaySound(wav, 1.0f, 1.0f, true, &first));
	REQUIRE(Forge::AudioEngine::PlaySound(wav, 1.0f, 1.0f, true, &second));

	REQUIRE(first != Forge::AudioEngine::InvalidSound);
	REQUIRE(second != Forge::AudioEngine::InvalidSound);
	REQUIRE(first != second);
	REQUIRE(Forge::AudioEngine::GetActiveSoundCount() == 2);

	// Stopping one sound must leave the other alone. The old fire-and-forget
	// implementation stopped the entire engine here, silencing everything.
	REQUIRE(Forge::AudioEngine::StopSound(first));
	REQUIRE(Forge::AudioEngine::IsValidSound(first) == false);
	REQUIRE(Forge::AudioEngine::IsValidSound(second) == true);
	REQUIRE(Forge::AudioEngine::GetActiveSoundCount() == 1);

	// A second stop of the same handle fails, which is how a double-stop is
	// detected rather than silently succeeding.
	REQUIRE(Forge::AudioEngine::StopSound(first) == false);

	Forge::AudioEngine::StopAllSounds();
	REQUIRE(Forge::AudioEngine::GetActiveSoundCount() == 0);

	Forge::AudioEngine::SetFadeDuration(0.02f);
	Forge::AudioEngine::Shutdown();
}

TEST_CASE("With fades enabled a stopped sound is released after its fade", "[audio][fade]")
{
	Forge::AudioEngine::Shutdown();

	if (!Forge::AudioEngine::Init())
	{
		SUCCEED("No audio device; deferred release requires one to verify");
		return;
	}

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_fade.wav", 48000, 2.0);

	Forge::AudioEngine::SetFadeDuration(0.05f);
	REQUIRE(Forge::AudioEngine::GetFadeDuration() == Approx(0.05f));

	Forge::AudioEngine::SoundHandle handle = Forge::AudioEngine::InvalidSound;
	REQUIRE(Forge::AudioEngine::PlaySound(wav, 1.0f, 1.0f, true, &handle));

	REQUIRE(Forge::AudioEngine::StopSound(handle));

	// Deferred: the sound keeps playing out its decay, because releasing it now
	// would cut the fade off mid-way, which is the click being avoided.
	REQUIRE(Forge::AudioEngine::IsValidSound(handle) == true);

	// A repeated stop while fading is rejected, so callers can distinguish
	// "stopped" from "stopping".
	REQUIRE(Forge::AudioEngine::StopSound(handle) == false);

	// Once the fade has elapsed the sound stops playing and Update() reclaims it.
	// Sleep so real audio time passes rather than spinning.
	std::this_thread::sleep_for(std::chrono::milliseconds(200));
	REQUIRE(Forge::AudioEngine::IsSoundPlaying(handle) == false);

	Forge::AudioEngine::Update();
	REQUIRE(Forge::AudioEngine::IsValidSound(handle) == false);
	REQUIRE(Forge::AudioEngine::GetActiveSoundCount() == 0);

	Forge::AudioEngine::SetFadeDuration(0.02f);
	Forge::AudioEngine::Shutdown();
}

TEST_CASE("Fade duration is clamped to a sane range", "[audio][fade]")
{
	Forge::Log::Init();

	Forge::AudioEngine::SetFadeDuration(0.25f);
	REQUIRE(Forge::AudioEngine::GetFadeDuration() == Approx(0.25f));

	// Negative would be nonsense; it clamps to instant rather than inverting.
	Forge::AudioEngine::SetFadeDuration(-5.0f);
	REQUIRE(Forge::AudioEngine::GetFadeDuration() == Approx(0.0f));

	Forge::AudioEngine::SetFadeDuration(0.02f);
	REQUIRE(Forge::AudioEngine::GetFadeDuration() == Approx(0.02f));
}

TEST_CASE("Update is safe with nothing playing", "[audio]")
{
	Forge::AudioEngine::Shutdown();
	REQUIRE_NOTHROW(Forge::AudioEngine::Update());
	REQUIRE(Forge::AudioEngine::GetActiveSoundCount() == 0);
}

TEST_CASE("Stopping all sounds releases every handle", "[audio]")
{
	Forge::AudioEngine::Shutdown();

	if (!Forge::AudioEngine::Init())
	{
		SUCCEED("No audio device; handle release requires one to verify");
		return;
	}

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_all.wav", 48000, 1.0);

	std::vector<Forge::AudioEngine::SoundHandle> handles;
	for (int i = 0; i < 4; i++)
	{
		Forge::AudioEngine::SoundHandle handle = Forge::AudioEngine::InvalidSound;
		REQUIRE(Forge::AudioEngine::PlaySound(wav, 1.0f, 1.0f, true, &handle));
		handles.push_back(handle);
	}

	REQUIRE(Forge::AudioEngine::GetActiveSoundCount() == 4);

	Forge::AudioEngine::StopAllSounds();
	REQUIRE(Forge::AudioEngine::GetActiveSoundCount() == 0);


	for (Forge::AudioEngine::SoundHandle handle : handles)
		REQUIRE(Forge::AudioEngine::IsValidSound(handle) == false);

	Forge::AudioEngine::Shutdown();
}

TEST_CASE("Master volume is clamped and reported", "[audio]")
{
	Forge::Log::Init();

	Forge::AudioEngine::SetMasterVolume(0.5f);
	REQUIRE(Forge::AudioEngine::GetMasterVolume() == Approx(0.5f));

	// Out-of-range values are clamped rather than stored raw.
	Forge::AudioEngine::SetMasterVolume(5.0f);
	REQUIRE(Forge::AudioEngine::GetMasterVolume() == Approx(1.0f));

	Forge::AudioEngine::SetMasterVolume(-1.0f);
	REQUIRE(Forge::AudioEngine::GetMasterVolume() == Approx(0.0f));

	Forge::AudioEngine::SetMasterVolume(1.0f);
}

TEST_CASE("Shutdown is safe when never initialized", "[audio]")
{
	Forge::Log::Init();
	REQUIRE_NOTHROW(Forge::AudioEngine::Shutdown());
	REQUIRE_NOTHROW(Forge::AudioEngine::Shutdown());
	REQUIRE(Forge::AudioEngine::IsInitialized() == false);
}

// ---------------------------------------------------------------------------
// AudioSourceComponent
// ---------------------------------------------------------------------------

TEST_CASE("AudioSourceComponent defaults", "[audio][component]")
{
	Forge::Scene scene;
	auto entity = scene.CreateEntity("Speaker");
	auto& audio = entity.AddComponent<Forge::AudioSourceComponent>();

	REQUIRE(audio.SourcePath.empty());
	REQUIRE(audio.Volume == Approx(1.0f));
	REQUIRE(audio.Pitch == Approx(1.0f));
	REQUIRE(audio.Loop == false);
	REQUIRE(audio.PlayOnStart == true);
	REQUIRE(audio.SoundHandle == Forge::AudioEngine::InvalidSound);
	REQUIRE(audio.Finished == false);
}

TEST_CASE("AudioSourceComponent round-trips every authored field", "[audio][serialization]")
{
	std::error_code ec;
	fs::create_directories("test_audio", ec);

	auto scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Music");

	auto& audio = entity.AddComponent<Forge::AudioSourceComponent>();
	audio.SourcePath = "resources/audio/theme.ogg";
	audio.Volume = 0.35f;
	audio.Pitch = 1.75f;
	audio.Loop = true;
	audio.PlayOnStart = false;
	audio.SoundHandle = 999;      // runtime state that must not be written
	audio.Finished = true;         // likewise

	const std::string path = "test_audio/audio_source.forge";
	Forge::SceneSerializer serializer(scene);
	REQUIRE(serializer.Serialize(path));

	auto reloaded = Forge::CreateRef<Forge::Scene>();
	Forge::SceneSerializer reader(reloaded);
	REQUIRE(reader.Deserialize(path));

	auto restored = reloaded->FindEntityByName("Music");
	REQUIRE(restored);

	const auto& r = restored.GetComponent<Forge::AudioSourceComponent>();
	REQUIRE(r.SourcePath == "resources/audio/theme.ogg");
	REQUIRE(r.Volume == Approx(0.35f));
	REQUIRE(r.Pitch == Approx(1.75f));
	REQUIRE(r.Loop == true);
	REQUIRE(r.PlayOnStart == false);

	// Per-session state must not survive a round-trip; a stale handle would
	// make the source believe a sound is already playing.
	REQUIRE(r.SoundHandle == Forge::AudioEngine::InvalidSound);
	REQUIRE(r.Finished == false);
}

// ---------------------------------------------------------------------------
// Scene integration
// ---------------------------------------------------------------------------

TEST_CASE("A scene with audio sources starts and stops cleanly", "[audio][scene]")
{
	Forge::Log::Init();

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_scene.wav", 48000, 1.0);

	Forge::Scene scene;
	auto source = scene.CreateEntity("Music");
	auto& audio = source.AddComponent<Forge::AudioSourceComponent>();
	audio.SourcePath = wav;
	audio.Loop = true;

	// Audio is optional, so this must not throw regardless of whether a device
	// exists.
	REQUIRE_NOTHROW(scene.OnRuntimeStart());

	if (Forge::AudioEngine::IsInitialized())
		REQUIRE(audio.SoundHandle != Forge::AudioEngine::InvalidSound);

	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));

	// Stopping must release the handle so the sound does not outlive playback.
	REQUIRE_NOTHROW(scene.OnRuntimeStop());
	REQUIRE(audio.SoundHandle == Forge::AudioEngine::InvalidSound);
}

TEST_CASE("An audio source with no path is inert, not an error", "[audio][scene]")
{
	Forge::Log::Init();

	Forge::Scene scene;
	auto source = scene.CreateEntity("Empty");
	auto& audio = source.AddComponent<Forge::AudioSourceComponent>();
	audio.SourcePath = "";   // deliberately unfinished

	REQUIRE_NOTHROW(scene.OnRuntimeStart());
	REQUIRE(audio.SoundHandle == Forge::AudioEngine::InvalidSound);
	REQUIRE_NOTHROW(scene.OnRuntimeStop());
}

TEST_CASE("PlayOnStart false does not start the source", "[audio][scene]")
{
	Forge::Log::Init();
	Forge::AudioEngine::Shutdown();

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_manual.wav", 48000, 1.0);

	Forge::Scene scene;
	auto source = scene.CreateEntity("Manual");
	auto& audio = source.AddComponent<Forge::AudioSourceComponent>();
	audio.SourcePath = wav;
	audio.PlayOnStart = false;

	scene.OnRuntimeStart();
	REQUIRE(audio.SoundHandle == Forge::AudioEngine::InvalidSound);

	scene.OnRuntimeStop();
}

TEST_CASE("A source pointing at a missing file does not break the scene", "[audio][scene]")
{
	Forge::Log::Init();

	Forge::Scene scene;

	auto broken = scene.CreateEntity("Broken");
	broken.AddComponent<Forge::AudioSourceComponent>().SourcePath = "test_audio/no_such_clip.wav";

	// A good source alongside the broken one, to prove one failure does not
	// abort the rest.
	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_alongside.wav", 48000, 1.0);
	auto good = scene.CreateEntity("Good");
	auto& goodAudio = good.AddComponent<Forge::AudioSourceComponent>();
	goodAudio.SourcePath = wav;
	goodAudio.PlayOnStart = true;

	REQUIRE_NOTHROW(scene.OnRuntimeStart());
	REQUIRE_NOTHROW(scene.OnUpdateRuntime(Forge::Timestep(0.016f)));
	REQUIRE_NOTHROW(scene.OnRuntimeStop());
}

// ---------------------------------------------------------------------------
// Lua
// ---------------------------------------------------------------------------

TEST_CASE("Lua PlaySound is callable and returns a boolean", "[scripting][audio]")
{
	Forge::Log::Init();
	Forge::ScriptEngine::Init();

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_lua.wav", 48000, 1.0);

	std::string error;
	const std::string code =
		"local ok, handle = PlaySound('" + wav + "', 1.0, 1.0, false)\n"
		"assert(type(ok) == 'boolean')\n"
		"assert(PlaySound('', 1.0, 1.0, false) == false)\n"
		"return true";

	// Must execute without error even with no audio device: the contract is that
	// it reports failure rather than raising.
	REQUIRE(Forge::ScriptEngine::ExecuteString(code, &error));

	Forge::ScriptEngine::Shutdown();
}

TEST_CASE("Lua entity PlaySound and StopSound are callable", "[scripting][audio]")
{
	Forge::Log::Init();
	Forge::ScriptEngine::Init();
	Forge::ScriptEngine::SetScriptDirectory(std::string(FORGE_SOURCE_DIR) + "/resources/scripts/");

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_lua_entity.wav", 48000, 1.0);

	Forge::Scene scene;
	auto entity = scene.CreateEntity("Speaker");
	entity.AddComponent<Forge::ScriptComponent>().ClassName = "Mover";

	Forge::ScriptEngine::OnRuntimeStart(&scene);

	std::string error;
	const std::string code =
		"local speaker = Scene:GetEntityByName('Speaker')\n"
		"assert(speaker ~= nil)\n"
		"assert(speaker:PlaySound('" + wav + "', 0.5, 1.0, false) ~= nil)\n"
		"assert(speaker:HasComponent('Audio'))\n"
		"assert(type(speaker:StopSound()) == 'boolean')\n"
		"return true";

	REQUIRE(Forge::ScriptEngine::ExecuteString(code, &error));

	Forge::ScriptEngine::OnRuntimeStop();
	Forge::ScriptEngine::Shutdown();
}
// ---------------------------------------------------------------------------
// 3D positional audio
//
// The spatial maths lives in the engine rather than miniaudio, so it is a pure
// function of positions and can be verified with no audio device at all. Every
// test below runs regardless of whether this machine has a sound card.
// ---------------------------------------------------------------------------

namespace {

	Forge::AudioEngine::SpatialParams MakeSpatial(float minDistance = 1.0f, float maxDistance = 20.0f, float rolloff = 1.0f)
	{
		Forge::AudioEngine::SpatialParams spatial;
		spatial.Enabled = true;
		spatial.MinDistance = minDistance;
		spatial.MaxDistance = maxDistance;
		spatial.Rolloff = rolloff;
		return spatial;
	}

	// A listener at the origin facing -Z, which is the engine's convention.
	Forge::AudioEngine::ListenerState DefaultListener()
	{
		Forge::AudioEngine::ListenerState listener;
		listener.Position = { 0.0f, 0.0f, 0.0f };
		listener.Forward = { 0.0f, 0.0f, -1.0f };
		listener.Up = { 0.0f, 1.0f, 0.0f };
		return listener;
	}

} // namespace

TEST_CASE("A non-spatial sound is full volume and centred", "[audio][spatial]")
{
	const auto mix = Forge::AudioEngine::ComputeSpatialMix(
		DefaultListener(), { 50.0f, 50.0f, 50.0f }, Forge::AudioEngine::SpatialParams{});

	REQUIRE(mix.Gain == Approx(1.0f));
	REQUIRE(mix.Pan == Approx(0.0f));
}

TEST_CASE("A spatial sound inside MinDistance is full volume", "[audio][spatial]")
{
	const auto spatial = MakeSpatial(2.0f, 20.0f);

	// Exactly at, and inside, the inner radius.
	for (float distance : { 0.0f, 0.5f, 1.0f, 2.0f })
	{
		const auto mix = Forge::AudioEngine::ComputeSpatialMix(
			DefaultListener(), { 0.0f, 0.0f, -distance }, spatial);
		REQUIRE(mix.Gain == Approx(1.0f));
	}
}

TEST_CASE("A spatial sound beyond MaxDistance is silent", "[audio][spatial]")
{
	const auto spatial = MakeSpatial(1.0f, 10.0f);

	for (float distance : { 10.0f, 25.0f, 1000.0f })
	{
		const auto mix = Forge::AudioEngine::ComputeSpatialMix(
			DefaultListener(), { 0.0f, 0.0f, -distance }, spatial);
		REQUIRE(mix.Gain == Approx(0.0f));
	}
}

TEST_CASE("Attenuation falls off monotonically between the radii", "[audio][spatial]")
{
	const auto spatial = MakeSpatial(1.0f, 11.0f);

	float previous = 1.0f;
	for (float distance = 1.0f; distance <= 11.0f; distance += 1.0f)
	{
		const auto mix = Forge::AudioEngine::ComputeSpatialMix(
			DefaultListener(), { 0.0f, 0.0f, -distance }, spatial);

		REQUIRE(mix.Gain <= previous);
		REQUIRE(mix.Gain >= 0.0f);
		REQUIRE(mix.Gain <= 1.0f);
		previous = mix.Gain;
	}

	// Linear rolloff over an even span should land at the midpoint.
	const auto mid = Forge::AudioEngine::ComputeSpatialMix(
		DefaultListener(), { 0.0f, 0.0f, -6.0f }, spatial);
	REQUIRE(mid.Gain == Approx(0.5f).margin(0.02f));
}

TEST_CASE("Rolloff shapes the attenuation curve", "[audio][spatial]")
{
	const glm::vec3 probe = { 0.0f, 0.0f, -5.5f };

	const auto gentle = Forge::AudioEngine::ComputeSpatialMix(DefaultListener(), probe, MakeSpatial(1.0f, 11.0f, 0.5f));
	const auto natural = Forge::AudioEngine::ComputeSpatialMix(DefaultListener(), probe, MakeSpatial(1.0f, 11.0f, 1.0f));
	const auto sharp = Forge::AudioEngine::ComputeSpatialMix(DefaultListener(), probe, MakeSpatial(1.0f, 11.0f, 2.0f));

	// Higher rolloff attenuates harder at the same distance.
	REQUIRE(gentle.Gain > natural.Gain);
	REQUIRE(natural.Gain > sharp.Gain);
}

TEST_CASE("Panning follows the listener's right axis", "[audio][spatial]")
{
	const auto spatial = MakeSpatial(1.0f, 50.0f);

	// Facing -Z, so the listener's right is +X.
	const auto ahead = Forge::AudioEngine::ComputeSpatialMix(DefaultListener(), { 0.0f, 0.0f, -5.0f }, spatial);
	const auto right = Forge::AudioEngine::ComputeSpatialMix(DefaultListener(), { 5.0f, 0.0f, 0.0f }, spatial);
	const auto left = Forge::AudioEngine::ComputeSpatialMix(DefaultListener(), { -5.0f, 0.0f, 0.0f }, spatial);
	const auto behind = Forge::AudioEngine::ComputeSpatialMix(DefaultListener(), { 0.0f, 0.0f, 5.0f }, spatial);

	// Dead ahead and dead behind are both centred; a listener should not hear a
	// source swing wide as it passes.
	REQUIRE(ahead.Pan == Approx(0.0f).margin(0.001f));
	REQUIRE(behind.Pan == Approx(0.0f).margin(0.001f));

	// Right is positive, left is negative.
	REQUIRE(right.Pan > 0.9f);
	REQUIRE(left.Pan < -0.9f);
}

TEST_CASE("Panning rotates with the listener", "[audio][spatial]")
{
	const auto spatial = MakeSpatial(1.0f, 50.0f);
	const glm::vec3 toTheRight = { 5.0f, 0.0f, 0.0f };

	// Facing -Z, the source is to the right.
	const auto facingDefault = Forge::AudioEngine::ComputeSpatialMix(
		DefaultListener(), toTheRight, spatial);
	REQUIRE(facingDefault.Pan > 0.9f);

	// Turn 90 degrees to face +X: the same source is now directly ahead.
	auto turned = DefaultListener();
	turned.Forward = { 1.0f, 0.0f, 0.0f };
	const auto facingTurned = Forge::AudioEngine::ComputeSpatialMix(
		turned, toTheRight, spatial);
	REQUIRE(facingTurned.Pan == Approx(0.0f).margin(0.001f));

	// Turn 90 more to face +Z. The same source, which was to the right, is now
	// to the listener's left, so the pan must swap sign.
	auto opposite = DefaultListener();
	opposite.Forward = { 0.0f, 0.0f, 1.0f };
	const auto facingOpposite = Forge::AudioEngine::ComputeSpatialMix(
		opposite, toTheRight, spatial);
	REQUIRE(facingOpposite.Pan < -0.9f);
}

TEST_CASE("A sound at the listener is centred, not NaN", "[audio][spatial]")
{
	const auto spatial = MakeSpatial(1.0f, 20.0f);

	// At the exact listener position there is no direction to pan towards;
	// dividing by the distance here would produce NaN.
	const auto mix = Forge::AudioEngine::ComputeSpatialMix(DefaultListener(), { 0.0f, 0.0f, 0.0f }, spatial);

	REQUIRE(mix.Gain == Approx(1.0f));
	REQUIRE(mix.Pan == Approx(0.0f));
	REQUIRE_FALSE(std::isnan(mix.Pan));
	REQUIRE_FALSE(std::isnan(mix.Gain));
}

TEST_CASE("Degenerate listener orientation is corrected", "[audio][audio]")
{
	Forge::AudioEngine::Shutdown();
	Forge::AudioEngine::Shutdown();   // reset to the default listener

	// A zero-length forward or up vector would make the pan maths produce NaN
	// for every sound, so SetListener substitutes a sane orientation.
	Forge::AudioEngine::ListenerState broken;
	broken.Position = { 1.0f, 2.0f, 3.0f };
	broken.Forward = { 0.0f, 0.0f, 0.0f };
	broken.Up = { 0.0f, 0.0f, 0.0f };

	Forge::AudioEngine::SetListener(broken);

	const auto& listener = Forge::AudioEngine::GetListener();
	REQUIRE(glm::length(listener.Forward) > 0.5f);
	REQUIRE(glm::length(listener.Up) > 0.5f);
	REQUIRE(listener.Position == glm::vec3(1.0f, 2.0f, 3.0f));

	// And spatialisation still produces finite results.
	const auto mix = Forge::AudioEngine::ComputeSpatialMix(
		listener, { 4.0f, 2.0f, 3.0f }, MakeSpatial());
	REQUIRE_FALSE(std::isnan(mix.Gain));
	REQUIRE_FALSE(std::isnan(mix.Pan));
}

TEST_CASE("Inverted radii do not produce undefined behaviour", "[audio][spatial]")
{
	const auto spatial = MakeSpatial(20.0f, 5.0f);   // min greater than max

	// A caller could author MaxDistance below MinDistance. It should not crash
	// or produce garbage.
	const auto mix = Forge::AudioEngine::ComputeSpatialMix(
		DefaultListener(), { 0.0f, 0.0f, -10.0f }, spatial);

	REQUIRE(std::isfinite(mix.Gain));
	REQUIRE(mix.Gain >= 0.0f);
	REQUIRE(mix.Gain <= 1.0f);
}

TEST_CASE("Moving the listener changes the mix", "[audio][spatial]")
{
	Forge::AudioEngine::Shutdown();

	const auto spatial = MakeSpatial(1.0f, 10.0f);
	const glm::vec3 source = { 0.0f, 0.0f, -5.0f };

	auto near = DefaultListener();
	near.Position = { 0.0f, 0.0f, 0.0f };
	Forge::AudioEngine::SetListener(near);
	Forge::AudioEngine::ListenerState saved = near;

	auto far = DefaultListener();
	far.Position = { 0.0f, 0.0f, 5.0f };   // 10 m away
	Forge::AudioEngine::SetListener(far);

	const auto afterFar = Forge::AudioEngine::ComputeSpatialMix(
		Forge::AudioEngine::GetListener(), source, spatial);

	Forge::AudioEngine::SetListener(saved);
	const auto afterNear = Forge::AudioEngine::ComputeSpatialMix(
		Forge::AudioEngine::GetListener(), source, spatial);

	REQUIRE(afterFar.Gain < afterNear.Gain);
	REQUIRE(afterFar.Gain == Approx(0.0f));
}

TEST_CASE("SetSoundPosition is safe for an unknown handle", "[audio][spatial]")
{
	Forge::AudioEngine::Shutdown();
	REQUIRE(Forge::AudioEngine::SetSoundPosition(4242, { 1.0f, 2.0f, 3.0f }) == false);
	REQUIRE_NOTHROW(Forge::AudioEngine::UpdateSpatialization());
}

TEST_CASE("AudioSourceComponent spatial fields round-trip", "[audio][serialization][spatial]")
{
	std::error_code ec;
	fs::create_directories("test_audio", ec);

	auto scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Footstep");

	auto& audio = entity.AddComponent<Forge::AudioSourceComponent>();
	audio.SourcePath = "resources/audio/step.wav";
	audio.Spatial = true;
	audio.MinDistance = 3.5f;
	audio.MaxDistance = 42.0f;

	const std::string path = "test_audio/audio_spatial.forge";
	Forge::SceneSerializer serializer(scene);
	REQUIRE(serializer.Serialize(path));

	auto reloaded = Forge::CreateRef<Forge::Scene>();
	Forge::SceneSerializer reader(reloaded);
	REQUIRE(reader.Deserialize(path));

	auto restored = reloaded->FindEntityByName("Footstep");
	REQUIRE(restored);

	const auto& r = restored.GetComponent<Forge::AudioSourceComponent>();
	REQUIRE(r.Spatial == true);
	REQUIRE(r.MinDistance == Approx(3.5f));
	REQUIRE(r.MaxDistance == Approx(42.0f));
}

TEST_CASE("A non-spatial source defaults to 2D behaviour", "[audio][component][spatial]")
{
	Forge::Scene scene;
	auto entity = scene.CreateEntity("Music");
	auto& audio = entity.AddComponent<Forge::AudioSourceComponent>();

	// Music and UI should default to playing at full volume regardless of where
	// they are in the world.
	REQUIRE(audio.Spatial == false);
}

// ---------------------------------------------------------------------------
// Mixing buses
//
// Ducking music under dialogue must be one call regardless of how many sounds
// are playing, which is what the bus abstraction buys. The gain maths is pure,
// so it is verified with no audio device.
// ---------------------------------------------------------------------------

TEST_CASE("Audio bus names round-trip through parsing", "[audio][bus]")
{
	using Forge::AudioBus;
	using Forge::AudioBusName;
	using Forge::ParseAudioBus;

	for (auto bus : { AudioBus::Master, AudioBus::Music, AudioBus::SoundEffects,
	                  AudioBus::Voice, AudioBus::Interface })
	{
		AudioBus parsed = AudioBus::Master;
		REQUIRE(ParseAudioBus(AudioBusName(bus), parsed));
		REQUIRE(parsed == bus);
	}
}

TEST_CASE("Bus names parse case-insensitively with aliases", "[audio][bus]")
{
	using Forge::AudioBus;
	using Forge::ParseAudioBus;

	AudioBus bus = AudioBus::Master;

	REQUIRE(ParseAudioBus("music", bus));
	REQUIRE(bus == AudioBus::Music);

	REQUIRE(ParseAudioBus("MUSIC", bus));
	REQUIRE(bus == AudioBus::Music);

	// Aliases so a script can use the obvious word.
	REQUIRE(ParseAudioBus("sfx", bus));
	REQUIRE(bus == AudioBus::SoundEffects);

	REQUIRE(ParseAudioBus("sound", bus));
	REQUIRE(bus == AudioBus::SoundEffects);

	REQUIRE(ParseAudioBus("ui", bus));
	REQUIRE(bus == AudioBus::Interface);

	// An unknown name is rejected rather than silently mapped somewhere wrong.
	REQUIRE(ParseAudioBus("NotABus", bus) == false);
	REQUIRE(ParseAudioBus("", bus) == false);
}

TEST_CASE("Every bus starts at full volume", "[audio][bus]")
{
	using Forge::AudioBus;
	using Forge::AudioEngine;

	for (auto bus : { AudioBus::Master, AudioBus::Music, AudioBus::SoundEffects,
	                  AudioBus::Voice, AudioBus::Interface })
		REQUIRE(Forge::AudioEngine::GetBusVolume(bus) == Approx(1.0f));
}

TEST_CASE("Bus volume is clamped to a valid range", "[audio][bus]")
{
	using Forge::AudioBus;
	using Forge::AudioEngine;

	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Music, 0.25f));
	REQUIRE(Forge::AudioEngine::GetBusVolume(AudioBus::Music) == Approx(0.25f));

	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Music, 5.0f));
	REQUIRE(Forge::AudioEngine::GetBusVolume(AudioBus::Music) == Approx(1.0f));

	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Music, -3.0f));
	REQUIRE(Forge::AudioEngine::GetBusVolume(AudioBus::Music) == Approx(0.0f));

	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Music, 1.0f));
}

TEST_CASE("Setting a bus by name works and unknown names fail", "[audio][bus]")
{
	using Forge::AudioEngine;

	REQUIRE(Forge::AudioEngine::SetBusVolume("Music", 0.4f));
	REQUIRE(Forge::AudioEngine::GetBusVolume("Music") == Approx(0.4f));

	// An unknown bus must be reported, not silently ignored: a typo that
	// silently did nothing would leave music playing over dialogue.
	REQUIRE(Forge::AudioEngine::SetBusVolume("NotABus", 0.1f) == false);
	REQUIRE(Forge::AudioEngine::GetBusVolume("NotABus") == Approx(1.0f));

	REQUIRE(Forge::AudioEngine::SetBusVolume("Music", 1.0f));
}

TEST_CASE("Buses mix independently", "[audio][bus]")
{
	using Forge::AudioBus;
	using Forge::AudioEngine;

	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Music, 0.5f));
	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::SoundEffects, 1.0f));

	// Ducking music must not touch effects.
	REQUIRE(Forge::AudioEngine::ComputeBusGain(AudioBus::Music) == Approx(0.5f));
	REQUIRE(Forge::AudioEngine::ComputeBusGain(AudioBus::SoundEffects) == Approx(1.0f));

	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Music, 1.0f));
}

TEST_CASE("Master volume multiplies into every bus", "[audio][bus]")
{
	using Forge::AudioBus;
	using Forge::AudioEngine;

	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Master, 0.5f));

	// Everything is heard through Master, so halving it halves every bus.
	REQUIRE(Forge::AudioEngine::ComputeBusGain(AudioBus::Music) == Approx(0.5f));
	REQUIRE(Forge::AudioEngine::ComputeBusGain(AudioBus::Voice) == Approx(0.5f));
	REQUIRE(Forge::AudioEngine::ComputeBusGain(AudioBus::Interface) == Approx(0.5f));

	// A bus set to full volume is still attenuated by a quiet Master.
	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Music, 1.0f));
	REQUIRE(Forge::AudioEngine::ComputeBusGain(AudioBus::Music) == Approx(0.5f));

	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Master, 1.0f));
	REQUIRE(Forge::AudioEngine::ComputeBusGain(AudioBus::Music) == Approx(1.0f));
}

TEST_CASE("Ducking music leaves voice untouched", "[audio][bus]")
{
	using Forge::AudioBus;
	using Forge::AudioEngine;

	// The exact effect this feature exists for.
	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Music, 0.3f));
	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Voice, 1.0f));

	REQUIRE(Forge::AudioEngine::ComputeBusGain(AudioBus::Music) == Approx(0.3f));
	REQUIRE(Forge::AudioEngine::ComputeBusGain(AudioBus::Voice) == Approx(1.0f));

	REQUIRE(Forge::AudioEngine::SetBusVolume(AudioBus::Music, 1.0f));
}

TEST_CASE("SetSoundBus is safe for an unknown handle", "[audio][bus]")
{
	Forge::AudioEngine::Shutdown();
	REQUIRE(Forge::AudioEngine::SetSoundBus(9999, Forge::AudioBus::Music) == false);

	Forge::AudioBus bus = Forge::AudioBus::Music;
	REQUIRE(Forge::AudioEngine::GetSoundBus(9999, bus) == false);
}

TEST_CASE("A sound reports the bus it was started on", "[audio][bus]")
{
	Forge::AudioEngine::Shutdown();

	if (!Forge::AudioEngine::Init())
	{
		SUCCEED("No audio device; bus routing requires one to verify");
		return;
	}

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_bus.wav", 48000, 1.0);

	Forge::AudioEngine::SoundHandle handle = Forge::AudioEngine::InvalidSound;
	REQUIRE(Forge::AudioEngine::PlaySoundOnBus(wav, Forge::AudioBus::Music, 1.0f, 1.0f, true, &handle));

	Forge::AudioBus bus = Forge::AudioBus::SoundEffects;
	REQUIRE(Forge::AudioEngine::GetSoundBus(handle, bus));
	REQUIRE(bus == Forge::AudioBus::Music);

	// Moving it between buses takes effect immediately.
	REQUIRE(Forge::AudioEngine::SetSoundBus(handle, Forge::AudioBus::Voice));
	REQUIRE(Forge::AudioEngine::GetSoundBus(handle, bus));
	REQUIRE(bus == Forge::AudioBus::Voice);

	Forge::AudioEngine::StopAllSounds();
	Forge::AudioEngine::Shutdown();
}

TEST_CASE("AudioSourceComponent bus round-trips", "[audio][bus][serialization]")
{
	std::error_code ec;
	fs::create_directories("test_audio", ec);

	auto scene = Forge::CreateRef<Forge::Scene>();
	auto entity = scene->CreateEntity("Theme");
	auto& audio = entity.AddComponent<Forge::AudioSourceComponent>();
	audio.SourcePath = "resources/audio/theme.ogg";
	audio.Bus = Forge::AudioBus::Music;

	const std::string path = "test_audio/audio_bus.forge";
	Forge::SceneSerializer serializer(scene);
	REQUIRE(serializer.Serialize(path));

	auto reloaded = Forge::CreateRef<Forge::Scene>();
	Forge::SceneSerializer reader(reloaded);
	REQUIRE(reader.Deserialize(path));

	auto restored = reloaded->FindEntityByName("Theme");
	REQUIRE(restored);
	REQUIRE(restored.GetComponent<Forge::AudioSourceComponent>().Bus == Forge::AudioBus::Music);
}

TEST_CASE("An unknown bus name in a scene file falls back to the default", "[audio][bus][serialization]")
{
	std::error_code ec;
	fs::create_directories("test_audio", ec);

	// A hand-edited or older scene file with a bad bus name must not lose the
	// whole scene; the source keeps its other fields.
	const std::string path = "test_audio/audio_bus_bad.forge";
	{
		std::ofstream out(path);
		out << "Scene: Untitled\n"
			<< "Entities:\n"
			<< "  - Entity: 12345\n"
			<< "    TagComponent:\n"
			<< "      Tag: BadBus\n"
			<< "    TransformComponent:\n"
			<< "      Translation: [0, 0, 0]\n"
			<< "      Rotation: [0, 0, 0]\n"
			<< "      Scale: [1, 1, 1]\n"
			<< "    AudioSourceComponent:\n"
			<< "      SourcePath: theme.ogg\n"
			<< "      Volume: 1\n"
			<< "      Pitch: 1\n"
			<< "      Loop: true\n"
			<< "      PlayOnStart: true\n"
			<< "      Bus: NotARealBus\n";
	}

	auto reloaded = Forge::CreateRef<Forge::Scene>();
	Forge::SceneSerializer reader(reloaded);
	REQUIRE(reader.Deserialize(path));

	auto restored = reloaded->FindEntityByName("BadBus");
	REQUIRE(restored);

	const auto& audio = restored.GetComponent<Forge::AudioSourceComponent>();
	// Falls back to the component default, and the rest of the source survives.
	REQUIRE(audio.Bus == Forge::AudioBus::SoundEffects);
	REQUIRE(audio.SourcePath == "theme.ogg");
	REQUIRE(audio.Loop == true);
}

TEST_CASE("Lua can set and read bus volumes", "[scripting][audio][bus]")
{
	Forge::Log::Init();
	Forge::ScriptEngine::Init();

	std::string error;
	const std::string code =
		"assert(SetBusVolume('Music', 0.35) == true)\n"
		"assert(math.abs(GetBusVolume('Music') - 0.35) < 0.001)\n"
		// An unknown bus must report failure rather than silently doing nothing.
		"assert(SetBusVolume('NotABus', 0.1) == false)\n"
		"assert(GetBusVolume('NotABus') == nil)\n"
		// Ducking must leave other buses alone.
		"assert(SetBusVolume('Voice', 1.0) == true)\n"
		"assert(math.abs(GetBusVolume('Voice') - 1.0) < 0.001)\n"
		"assert(SetBusVolume('Music', 1.0) == true)\n"
		"return true";

	REQUIRE(Forge::ScriptEngine::ExecuteString(code, &error));

	Forge::ScriptEngine::Shutdown();
}

TEST_CASE("Lua PlaySound accepts a bus name and rejects a bad one", "[scripting][audio][bus]")
{
	Forge::Log::Init();
	Forge::ScriptEngine::Init();

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_lua_bus.wav", 48000, 1.0);

	std::string error;
	const std::string code =
		"local ok = PlaySound('" + wav + "', 1.0, 1.0, false, 'Music')\n"
		"assert(type(ok) == 'boolean')\n"
		// An unknown bus is reported, not silently routed to SFX.
		"local bad = PlaySound('" + wav + "', 1.0, 1.0, false, 'NotABus')\n"
		"assert(bad == false)\n"
		"return true";

	REQUIRE(Forge::ScriptEngine::ExecuteString(code, &error));

	Forge::ScriptEngine::Shutdown();
}

// ---------------------------------------------------------------------------
// Gapless looping
//
// A looping sound needs the LOOPING flag on its data source, not just
// ma_sound_set_looping(); without it the resource manager stops filling its
// buffer at the end of the clip and the wrap is audible as a seam.
// ---------------------------------------------------------------------------

TEST_CASE("Short clips are buffered rather than streamed", "[audio][loop]")
{
	using Forge::AudioEngine;

	const uint64_t threshold = AudioEngine::GetStreamingThreshold();
	REQUIRE(threshold > 0);

	// A short sound effect must be buffered whole: the resource manager
	// pre-fills its stream buffer at init, and streaming something shorter than
	// that fill glitches audibly.
	REQUIRE(AudioEngine::ShouldStream(0, false) == false);
	REQUIRE(AudioEngine::ShouldStream(1024, false) == false);
	REQUIRE(AudioEngine::ShouldStream(threshold, false) == false);
	REQUIRE(AudioEngine::ShouldStream(threshold - 1, false) == false);
}

TEST_CASE("Large clips are streamed", "[audio][loop]")
{
	using Forge::AudioEngine;

	const uint64_t threshold = AudioEngine::GetStreamingThreshold();

	// Long music should not be held entirely in memory.
	REQUIRE(AudioEngine::ShouldStream(threshold + 1, false) == true);
	REQUIRE(AudioEngine::ShouldStream(threshold * 10, false) == true);
}

TEST_CASE("Short looping clips are still buffered", "[audio][loop]")
{
	using Forge::AudioEngine;

	const uint64_t threshold = AudioEngine::GetStreamingThreshold();

	// This is the case that actually matters for gapless looping: streaming a
	// short loop produces a glitch, so it must be buffered even though it loops.
	REQUIRE(AudioEngine::ShouldStream(4096, true) == false);
	REQUIRE(AudioEngine::ShouldStream(threshold - 1, true) == false);
	REQUIRE(AudioEngine::ShouldStream(threshold + 1, true) == true);
}

TEST_CASE("Streaming threshold is configurable", "[audio][loop]")
{
	using Forge::AudioEngine;

	const uint64_t original = AudioEngine::GetStreamingThreshold();

	AudioEngine::SetStreamingThreshold(1024);
	REQUIRE(AudioEngine::GetStreamingThreshold() == 1024);
	REQUIRE(AudioEngine::ShouldStream(2048, false) == true);
	REQUIRE(AudioEngine::ShouldStream(512, false) == false);

	// A zero threshold means everything streams; it must not divide by zero or
	// misbehave.
	AudioEngine::SetStreamingThreshold(0);
	REQUIRE(AudioEngine::GetStreamingThreshold() == 0);
	REQUIRE(AudioEngine::ShouldStream(1, false) == true);

	AudioEngine::SetStreamingThreshold(original);
	REQUIRE(AudioEngine::GetStreamingThreshold() == original);
}

TEST_CASE("A looping sound starts and keeps playing", "[audio][loop]")
{
	Forge::AudioEngine::Shutdown();

	if (!Forge::AudioEngine::Init())
	{
		SUCCEED("No audio device; looping playback requires one to verify");
		return;
	}

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_loop.wav", 48000, 0.5);

	Forge::AudioEngine::SoundHandle handle = Forge::AudioEngine::InvalidSound;
	REQUIRE(Forge::AudioEngine::PlaySound(wav, 1.0f, 1.0f, /*loop*/ true, &handle));

	// Let real audio time pass so the clip has certainly run past its end. A
	// non-looping sound would have finished by now; a looping one has not.
	std::this_thread::sleep_for(std::chrono::milliseconds(700));

	REQUIRE(Forge::AudioEngine::IsValidSound(handle));
	REQUIRE(Forge::AudioEngine::IsSoundPlaying(handle) == true);

	Forge::AudioEngine::StopAllSounds();
	Forge::AudioEngine::Shutdown();
}

TEST_CASE("A non-looping short sound ends on its own", "[audio][loop]")
{
	Forge::AudioEngine::Shutdown();

	if (!Forge::AudioEngine::Init())
	{
		SUCCEED("No audio device; one-shot playback requires one to verify");
		return;
	}

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_oneshot.wav", 48000, 0.2);

	Forge::AudioEngine::SoundHandle handle = Forge::AudioEngine::InvalidSound;
	REQUIRE(Forge::AudioEngine::PlaySound(wav, 1.0f, 1.0f, /*loop*/ false, &handle));

	// Past the clip's end, a one-shot must have stopped.
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	REQUIRE(Forge::AudioEngine::IsSoundPlaying(handle) == false);

	Forge::AudioEngine::StopAllSounds();
	Forge::AudioEngine::Shutdown();
}

TEST_CASE("The Scene keeps a looping source alive across frames", "[audio][loop][scene]")
{
	Forge::Log::Init();
	Forge::AudioEngine::Shutdown();

	if (!Forge::AudioEngine::Init())
	{
		SUCCEED("No audio device; scene looping requires one to verify");
		return;
	}

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_scene_loop.wav", 48000, 0.2);

	Forge::Scene scene;
	auto music = scene.CreateEntity("Music");
	auto& audio = music.AddComponent<Forge::AudioSourceComponent>();
	audio.SourcePath = wav;
	audio.Loop = true;
	audio.PlayOnStart = true;
	audio.Bus = Forge::AudioBus::Music;

	scene.OnRuntimeStart();
	REQUIRE(audio.SoundHandle != Forge::AudioEngine::InvalidSound);

	// Many frames, far longer than the clip: a looping source must never be
	// reclaimed as though it had finished.
	for (int i = 0; i < 120; i++)
	{
		scene.OnUpdateRuntime(Forge::Timestep(1.0f / 60.0f));
		REQUIRE(audio.SoundHandle != Forge::AudioEngine::InvalidSound);
		REQUIRE(audio.Finished == false);
	}

	scene.OnRuntimeStop();
	REQUIRE(audio.SoundHandle == Forge::AudioEngine::InvalidSound);

	Forge::AudioEngine::Shutdown();
}

TEST_CASE("The Scene reclaims a finished one-shot", "[audio][loop][scene]")
{
	Forge::Log::Init();
	Forge::AudioEngine::Shutdown();

	if (!Forge::AudioEngine::Init())
	{
		SUCCEED("No audio device; one-shot reclaim requires one to verify");
		return;
	}

	const std::string wav = WriteSineWav(AudioFixtureDir() + "tone_scene_oneshot.wav", 48000, 0.15);

	Forge::Scene scene;
	auto sfx = scene.CreateEntity("Blip");
	auto& audio = sfx.AddComponent<Forge::AudioSourceComponent>();
	audio.SourcePath = wav;
	audio.Loop = false;
	audio.PlayOnStart = true;

	scene.OnRuntimeStart();
	REQUIRE(audio.SoundHandle != Forge::AudioEngine::InvalidSound);

	// Past the clip's end the source must release its handle rather than
	// holding a finished sound for the whole session.
	std::this_thread::sleep_for(std::chrono::milliseconds(400));
	for (int i = 0; i < 5; i++)
		scene.OnUpdateRuntime(Forge::Timestep(1.0f / 60.0f));

	REQUIRE(audio.SoundHandle == Forge::AudioEngine::InvalidSound);
	REQUIRE(audio.Finished == true);

	scene.OnRuntimeStop();
	Forge::AudioEngine::Shutdown();
}

// ---------------------------------------------------------------------------
// Volume is a single source of truth
// ---------------------------------------------------------------------------

TEST_CASE("Effective gain composes authored volume with the bus", "[audio][volume]")
{
	Forge::Log::Init();
	using Forge::AudioEngine;
	using Forge::AudioBus;

	// Full volume on a full bus is unity.
	REQUIRE(AudioEngine::ComputeEffectiveGain(1.0f, AudioBus::Master) == Approx(1.0f));

	// Half the authored volume is half the mix.
	REQUIRE(AudioEngine::ComputeEffectiveGain(0.5f, AudioBus::Master) == Approx(0.5f));

	// A ducked bus multiplies through rather than replacing the author's intent,
	// which is what lets one call reach a whole category of sounds.
	REQUIRE(AudioEngine::SetBusVolume(AudioBus::Music, 0.25f));
	REQUIRE(AudioEngine::ComputeEffectiveGain(1.0f, AudioBus::Music) == Approx(0.25f));
	REQUIRE(AudioEngine::ComputeEffectiveGain(0.5f, AudioBus::Music) == Approx(0.125f));

	// Master ducks every other bus, so it multiplies in on top of that.
	REQUIRE(AudioEngine::SetBusVolume(AudioBus::Master, 0.5f));
	REQUIRE(AudioEngine::ComputeEffectiveGain(1.0f, AudioBus::Music) == Approx(0.125f));
	REQUIRE(AudioEngine::ComputeEffectiveGain(1.0f, AudioBus::SoundEffects) == Approx(0.5f));

	REQUIRE(AudioEngine::SetBusVolume(AudioBus::Master, 1.0f));
	REQUIRE(AudioEngine::SetBusVolume(AudioBus::Music, 1.0f));
}

TEST_CASE("SetSoundVolume survives the next spatialisation update", "[audio][volume]")
{
	Forge::Log::Init();
	using Forge::AudioEngine;
	using Forge::AudioBus;

	REQUIRE(AudioEngine::Init());

	const std::string wav = WriteSineWav(AudioFixtureDir() + "volume.wav", 48000, 2.0);

	AudioEngine::SoundHandle handle = AudioEngine::InvalidSound;
	REQUIRE(AudioEngine::PlaySound(wav, 1.0f, 1.0f, true, &handle));

	REQUIRE(AudioEngine::SetSoundVolume(handle, 0.25f));

	// Applied immediately, not deferred to the next frame.
	REQUIRE(AudioEngine::GetSoundVolume(handle) == Approx(0.25f));

	// The regression this pins: spatialisation recomputes gain every frame from
	// the authored volume. A setter that wrote only to the ma_sound was silently
	// reverted here, and lost the bus gain with it.
	AudioEngine::Update();
	REQUIRE(AudioEngine::GetSoundVolume(handle) == Approx(0.25f));

	AudioEngine::Update();
	REQUIRE(AudioEngine::GetSoundVolume(handle) == Approx(0.25f));

	// A bus change must also compose with it rather than replace it.
	REQUIRE(AudioEngine::SetBusVolume(AudioBus::SoundEffects, 0.5f));
	AudioEngine::Update();
	REQUIRE(AudioEngine::GetSoundVolume(handle) == Approx(0.125f));
	REQUIRE(AudioEngine::SetBusVolume(AudioBus::SoundEffects, 1.0f));

	// Out-of-range requests clamp instead of passing through to the mixer.
	REQUIRE(AudioEngine::SetSoundVolume(handle, 5.0f));
	REQUIRE(AudioEngine::GetSoundVolume(handle) == Approx(1.0f));

	REQUIRE(AudioEngine::SetSoundVolume(handle, -1.0f));
	REQUIRE(AudioEngine::GetSoundVolume(handle) == Approx(0.0f));

	// An unknown handle is still reported rather than silently accepted.
	REQUIRE(AudioEngine::SetSoundVolume(AudioEngine::InvalidSound, 0.5f) == false);
	REQUIRE(AudioEngine::GetSoundVolume(AudioEngine::InvalidSound) == Approx(-1.0f));

	AudioEngine::StopAllSounds();
	AudioEngine::Shutdown();
}

TEST_CASE("Volume changes reach a spatial sound with its attenuation", "[audio][volume]")
{
	Forge::Log::Init();
	using Forge::AudioEngine;
	using Forge::AudioBus;

	REQUIRE(AudioEngine::Init());

	const std::string wav = WriteSineWav(AudioFixtureDir() + "volume_spatial.wav", 48000, 2.0);

	// A source far enough away to be attenuated, so its mix is strictly below
	// the authored volume.
	AudioEngine::SpatialParams spatial;
	spatial.Enabled = true;
	spatial.MinDistance = 1.0f;
	spatial.MaxDistance = 20.0f;
	spatial.Rolloff = 1.0f;

	AudioEngine::SoundHandle handle = AudioEngine::InvalidSound;
	const glm::vec3 position(10.0f, 0.0f, 0.0f);
	REQUIRE(AudioEngine::PlaySoundAtOnBus(wav, position, spatial, AudioBus::SoundEffects,
		1.0f, 1.0f, false, &handle));

	const float attenuated = AudioEngine::GetSoundVolume(handle);
	REQUIRE(attenuated < 1.0f);
	REQUIRE(attenuated > 0.0f);

	// Halving the authored volume must halve the whole mix, attenuation included.
	REQUIRE(AudioEngine::SetSoundVolume(handle, 0.5f));
	REQUIRE(AudioEngine::GetSoundVolume(handle) == Approx(attenuated * 0.5f));

	// And it must stay put across a recompute, which is where the old setter
	// lost both the authored change and the spatial gain.
	AudioEngine::Update();
	REQUIRE(AudioEngine::GetSoundVolume(handle) == Approx(attenuated * 0.5f));

	AudioEngine::StopAllSounds();
	AudioEngine::Shutdown();
}
