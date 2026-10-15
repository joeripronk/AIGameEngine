#pragma once

#include "Forge/Core/Base.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

struct ma_engine;
struct ma_sound;

namespace Forge {

	// A decoded, in-memory audio clip. Decoding is device independent, so
	// loading works even without a sound card (e.g. in tests/headless).
	class AudioBuffer
	{
	public:
		AudioBuffer() = default;
		~AudioBuffer();

		AudioBuffer(const AudioBuffer&) = delete;
		AudioBuffer& operator=(const AudioBuffer&) = delete;

		uint32_t GetChannels() const { return m_Channels; }
		uint32_t GetSampleRate() const { return m_SampleRate; }
		uint64_t GetFrameCount() const { return m_FrameCount; }
		float GetDuration() const;
		const float* GetSamples() const { return m_Samples; }

		// The source path, retained so playback can (re)load it through miniaudio.
		const std::string& GetSourcePath() const { return m_SourcePath; }

	private:
		friend class AudioEngine;

		std::string m_SourcePath;
		uint32_t m_Channels = 0;
		uint32_t m_SampleRate = 0;
		uint64_t m_FrameCount = 0;
		float* m_Samples = nullptr;  // interleaved PCM
	};

	// Audio playback, backed by miniaudio's `ma_engine` mixer.
	//
	// Every playing sound has its own handle, so a sound can be stopped, pitched
	// or re-volumed without disturbing any other. (miniaudio's fire-and-forget
	// ma_engine_play_sound has no handle, which meant stopping one sound stopped
	// all of them.)
	//
	// Audio is optional: when no output device is available Init() fails and
	// every playback call returns false. Nothing in the engine treats audio as
	// fatal, so a machine with no sound card runs the game silently rather than
	// refusing to start.
	class AudioEngine
	{
	public:
		// How a sound is positioned relative to the listener.
		struct SpatialParams
		{
			bool Enabled = false;

			// Inside MinDistance a sound plays at full volume. Between MinDistance
			// and MaxDistance it falls off; beyond MaxDistance it is silent.
			float MinDistance = 1.0f;
			float MaxDistance = 20.0f;

			// Exponent on the normalised distance. 1 is linear (natural), higher
			// values fall off faster, lower values carry further.
			float Rolloff = 1.0f;
		};

		// The listener's pose. Defaults to the origin looking down -Z, which
		// matches the engine's camera convention.
		struct ListenerState
		{
			glm::vec3 Position = { 0.0f, 0.0f, 0.0f };
			glm::vec3 Forward = { 0.0f, 0.0f, -1.0f };
			glm::vec3 Up = { 0.0f, 1.0f, 0.0f };
		};

		// Result of spatialising one sound.
		struct SpatialMix
		{
			float Gain = 1.0f;   // linear 0..1 volume multiplier
			float Pan = 0.0f;    // stereo, -1 hard left .. +1 hard right
		};

		// Opaque, stable identifier for a playing sound. Valid until the sound is
		// stopped or the engine shuts down.
		using SoundHandle = uint64_t;
		static constexpr SoundHandle InvalidSound = 0;

		[[nodiscard]] static bool Init();
		static void Shutdown();
		static bool IsInitialized();

		// Decodes an audio file (WAV/MP3/FLAC/OGG via miniaudio) into PCM.
		// Works without an output device.
		[[nodiscard]] static Ref<AudioBuffer> LoadAudioBuffer(const std::string& filepath);

		// Starts a sound, returning its handle in `outHandle`. `filepath` is
		// streamed by the engine, so this is cheap and does not require the whole
		// file to be resident.
		//
		// Pass `outHandle` to receive the new sound's handle. It is set to
		// InvalidSound when playback could not start.
		[[nodiscard]] static bool PlaySound(const std::string& filepath,
			float volume = 1.0f, float pitch = 1.0f, bool loop = false,
			SoundHandle* outHandle = nullptr);

		// As above, but positioned in the world and attenuated/panned relative to
		// the listener. `position` is ignored when `spatial.Enabled` is false.
		[[nodiscard]] static bool PlaySoundAt(const std::string& filepath,
			const glm::vec3& position, const SpatialParams& spatial,
			float volume = 1.0f, float pitch = 1.0f, bool loop = false,
			SoundHandle* outHandle = nullptr);

		// --- Click-free playback ---
		//
		// Setting volume or stopping a sound instantly produces an audible click:
		// the waveform jumps discontinuously. Sounds therefore fade in on start,
		// ramp volume changes, and fade out before release.
		//
		// StopSound begins a fade-out and the sound is reclaimed on the next
		// Update(), because it must keep playing for the duration of the fade.

		// Fade duration used for starting and stopping sounds, in seconds.
		// Defaults to 0.02 (20 ms), long enough to kill a click and short enough
		// to feel immediate. Set to 0 for instant, clicking playback.
		static void SetFadeDuration(float seconds);
		static float GetFadeDuration();

		// Per-frame housekeeping: reclaims sounds whose fade-out has finished and
		// re-mixes spatial sounds. The Scene calls this once per frame.
		static void Update();

		// --- 3D audio ---
		//
		// Spatialisation is computed here rather than delegated to miniaudio,
		// because this version of the library exposes no listener accessor. Doing
		// it in the engine keeps it a pure function of positions, which means it
		// is unit-testable with no audio device at all.

		// The listener's pose, normally driven by the primary camera.
		static void SetListener(const ListenerState& listener);
		static const ListenerState& GetListener();

		// Moves an already-playing sound. Ignored for non-spatial sounds.
		[[nodiscard]] static bool SetSoundPosition(SoundHandle handle, const glm::vec3& position);

		// Recomputes gain and pan for every spatial sound. Call once per frame
		// after moving the listener and any sources.
		static void UpdateSpatialization();

		// Pure spatial maths: distance attenuation and stereo pan of `source`
		// relative to `listener`. Exposed so the curve can be verified without a
		// sound card.
		static SpatialMix ComputeSpatialMix(const ListenerState& listener,
			const glm::vec3& source, const SpatialParams& spatial);

		[[nodiscard]] static bool StopSound(SoundHandle handle);
		[[nodiscard]] static bool SetSoundVolume(SoundHandle handle, float volume);
		[[nodiscard]] static bool SetSoundPitch(SoundHandle handle, float pitch);

		static bool IsSoundPlaying(SoundHandle handle);
		static bool IsValidSound(SoundHandle handle);

		// Stops and releases every sound.
		static void StopAllSounds();

		// Sounds currently held by the engine, playing or finished-but-not-yet
		// reclaimed. Primarily for diagnostics and tests.
		static uint32_t GetActiveSoundCount();

		// Convenience wrappers taking a decoded buffer, which retains the path it
		// came from.
		[[nodiscard]] static bool PlayAudioBuffer(const Ref<AudioBuffer>& buffer);
		[[nodiscard]] static bool StopAudioBuffer(const Ref<AudioBuffer>& buffer);

		// Global volume in [0, 1] (default 1.0).
		static void SetMasterVolume(float volume);
		static float GetMasterVolume();

	private:
		// Fade length in PCM frames, or 0 when fading is disabled.
		static uint64_t FadeFrames();

		// Uninitialises and frees a sound, invalidating its handle.
		static void ReleaseSound(SoundHandle handle);

		struct PlayingSound
		{
			ma_sound* Sound = nullptr;
			std::string SourcePath;

			// Authored volume, kept separate from the spatialised volume so
			// spatialisation can be recomputed each frame without compounding
			// attenuation or losing the author's intent.
			float BaseVolume = 1.0f;

			SpatialParams Spatial;
			glm::vec3 Position = { 0.0f, 0.0f, 0.0f };

			// True once a fade-out has been requested. The sound keeps playing
			// until the fade completes, so it must not be reclaimed yet.
			bool PendingRelease = false;
		};

		static PlayingSound* Find(SoundHandle handle);

		// Shared implementation behind PlaySound and PlaySoundAt.
		[[nodiscard]] static bool StartSound(const std::string& filepath,
			float volume, float pitch, bool loop,
			const glm::vec3* position, const SpatialParams& spatial,
			SoundHandle* outHandle);

		static ma_engine* s_Engine;
		static float s_MasterVolume;
		static float s_FadeDuration;
		static ListenerState s_Listener;
		static std::unordered_map<SoundHandle, PlayingSound> s_Sounds;
		static SoundHandle s_NextHandle;
	};

}