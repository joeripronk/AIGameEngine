#pragma once

#include "Forge/Core/Base.h"

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
		struct PlayingSound
		{
			ma_sound* Sound = nullptr;
			std::string SourcePath;
		};

		static PlayingSound* Find(SoundHandle handle);

		static ma_engine* s_Engine;
		static float s_MasterVolume;
		static std::unordered_map<SoundHandle, PlayingSound> s_Sounds;
		static SoundHandle s_NextHandle;
	};

}