#pragma once

#include "Forge/Core/Base.h"

#include <string>

struct ma_engine;

namespace Forge {

	// A decoded, in-memory audio clip. Decoding is device independent, so
	// loading works even without a sound card (e.g. in tests/headless).
	class AudioBuffer
	{
	public:
		AudioBuffer() = default;
		~AudioBuffer();

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

	// High-level audio engine backed by miniaudio's `ma_engine` mixer.
	class AudioEngine
	{
	public:
		static bool Init();
		static void Shutdown();
		static bool IsInitialized();

		// Decodes an audio file (WAV/MP3/FLAC/OGG via miniaudio) into PCM.
		// Works without an output device.
		static Ref<AudioBuffer> LoadAudioBuffer(const std::string& filepath);

		// Playback (no-op / false when no output device is available).
		static bool PlayAudioBuffer(const Ref<AudioBuffer>& buffer);
		static bool StopAudioBuffer(const Ref<AudioBuffer>& buffer);

		// Global volume in [0, 1] (default 1.0).
		static void SetMasterVolume(float volume);
		static float GetMasterVolume();

	private:
		static ma_engine* s_Engine;
		static float s_MasterVolume;
	};

}