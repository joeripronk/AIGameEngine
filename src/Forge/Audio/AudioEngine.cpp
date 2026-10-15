#include "fgpch.h"
#include "Forge/Audio/AudioEngine.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace Forge {

	ma_engine* AudioEngine::s_Engine = nullptr;
	float AudioEngine::s_MasterVolume = 1.0f;
	std::unordered_map<AudioEngine::SoundHandle, AudioEngine::PlayingSound> AudioEngine::s_Sounds;
	AudioEngine::SoundHandle AudioEngine::s_NextHandle = 1; // 0 is InvalidSound

	// --- AudioBuffer ------------------------------------------------------

	AudioBuffer::~AudioBuffer()
	{
		delete[] m_Samples;
		m_Samples = nullptr;
	}

	float AudioBuffer::GetDuration() const
	{
		if (m_SampleRate == 0)
			return 0.0f;
		return (float)m_FrameCount / (float)m_SampleRate;
	}

	// --- AudioEngine ------------------------------------------------------

	bool AudioEngine::Init()
	{
		if (s_Engine)
			return true;

		ma_engine* engine = new (std::nothrow) ma_engine();
		if (!engine)
		{
			FG_CORE_ERROR("AudioEngine: out of memory");
			return false;
		}

		ma_engine_config config = ma_engine_config_init();
		config.channels = 2;
		config.sampleRate = 48000;

		ma_result result = ma_engine_init(&config, engine);
		if (result != MA_SUCCESS)
		{
			// The common case: no output device (CI, container, headless).
			// Audio is optional, so this is a warning rather than an error.
			FG_CORE_WARN("AudioEngine: no output device ({0}); running without audio", (int)result);
			delete engine;
			s_Engine = nullptr;
			return false;
		}

		s_Engine = engine;
		ma_engine_set_volume(s_Engine, s_MasterVolume);
		FG_CORE_INFO("AudioEngine initialized");
		return true;
	}

	void AudioEngine::Shutdown()
	{
		// Release sounds before the engine they are attached to.
		StopAllSounds();

		if (!s_Engine)
			return;

		ma_engine_uninit(s_Engine);
		delete s_Engine;
		s_Engine = nullptr;
		FG_CORE_INFO("AudioEngine shut down");
	}

	bool AudioEngine::IsInitialized()
	{
		return s_Engine != nullptr;
	}

	Ref<AudioBuffer> AudioEngine::LoadAudioBuffer(const std::string& filepath)
	{
		// Decode independently of the engine so loading works headless.
		ma_decoder_config decoderConfig = ma_decoder_config_init(ma_format_f32, 2, 48000);

		ma_decoder decoder;
		ma_result result = ma_decoder_init_file(filepath.c_str(), &decoderConfig, &decoder);
		if (result != MA_SUCCESS)
		{
			FG_CORE_ERROR("AudioEngine: failed to decode '{0}' ({1})", filepath, (int)result);
			return nullptr;
		}

		ma_uint64 frameCount = 0;
		ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);

		if (frameCount == 0)
		{
			ma_decoder_uninit(&decoder);
			FG_CORE_ERROR("AudioEngine: '{0}' contains no audio frames", filepath);
			return nullptr;
		}

		auto buffer = CreateRef<AudioBuffer>();
		buffer->m_Samples = new (std::nothrow) float[(size_t)frameCount * 2];

		if (!buffer->m_Samples)
		{
			ma_decoder_uninit(&decoder);
			FG_CORE_ERROR("AudioEngine: out of memory decoding '{0}'", filepath);
			return nullptr;
		}

		buffer->m_SourcePath = filepath;

		ma_uint64 framesRead = 0;
		result = ma_decoder_read_pcm_frames(&decoder, buffer->m_Samples, frameCount, &framesRead);
		ma_decoder_uninit(&decoder);

		if (result != MA_SUCCESS && result != MA_AT_END)
		{
			FG_CORE_ERROR("AudioEngine: decode read failed for '{0}' ({1})", filepath, (int)result);
			return nullptr;
		}

		buffer->m_FrameCount = framesRead;
		buffer->m_Channels = 2;
		buffer->m_SampleRate = 48000;
		return buffer;
	}

	AudioEngine::PlayingSound* AudioEngine::Find(SoundHandle handle)
	{
		if (handle == InvalidSound)
			return nullptr;

		auto it = s_Sounds.find(handle);
		return it == s_Sounds.end() ? nullptr : &it->second;
	}

	bool AudioEngine::PlaySound(const std::string& filepath, float volume, float pitch, bool loop,
		SoundHandle* outHandle)
	{
		if (outHandle)
			*outHandle = InvalidSound;

		if (!s_Engine)
			return false;

		if (filepath.empty())
		{
			FG_CORE_WARN("AudioEngine: cannot play a sound with no path");
			return false;
		}

		auto* sound = new (std::nothrow) ma_sound();
		if (!sound)
		{
			FG_CORE_ERROR("AudioEngine: out of memory allocating sound for '{0}'", filepath);
			return false;
		}

		ma_sound_config config = ma_sound_config_init();
		config.pFilePath = filepath.c_str();
		config.channelsIn = 0;   // use the engine's channel count
		config.channelsOut = 0;  // use the engine's channel count
		config.isLooping = loop ? 1 : 0;

		// The resource manager loads asynchronously, so this returns promptly and
		// the sound begins once the first pages are decoded.
		ma_result result = ma_sound_init_ex(s_Engine, &config, sound);
		if (result != MA_SUCCESS)
		{
			FG_CORE_WARN("AudioEngine: failed to start '{0}' ({1})", filepath, (int)result);
			delete sound;
			return false;
		}

		result = ma_sound_start(sound);
		if (result != MA_SUCCESS)
		{
			FG_CORE_WARN("AudioEngine: failed to play '{0}' ({1})", filepath, (int)result);
			ma_sound_uninit(sound);
			delete sound;
			return false;
		}

		ma_sound_set_volume(sound, (volume < 0.0f) ? 0.0f : ((volume > 1.0f) ? 1.0f : volume));
		ma_sound_set_pitch(sound, (pitch <= 0.0f) ? 1.0f : pitch);
		ma_sound_set_looping(sound, loop ? 1 : 0);

		const SoundHandle handle = s_NextHandle++;
		PlayingSound& entry = s_Sounds[handle];
		entry.Sound = sound;
		entry.SourcePath = filepath;

		if (outHandle)
			*outHandle = handle;

		return true;
	}

	bool AudioEngine::StopSound(SoundHandle handle)
	{
		PlayingSound* entry = Find(handle);
		if (!entry)
			return false;

		ma_sound_stop(entry->Sound);
		ma_sound_uninit(entry->Sound);
		delete entry->Sound;

		// Erasing the entry invalidates the handle, which is what lets callers
		// detect a double stop instead of silently succeeding twice.
		s_Sounds.erase(handle);
		return true;
	}

	bool AudioEngine::SetSoundVolume(SoundHandle handle, float volume)
	{
		PlayingSound* entry = Find(handle);
		if (!entry)
			return false;

		ma_sound_set_volume(entry->Sound, (volume < 0.0f) ? 0.0f : ((volume > 1.0f) ? 1.0f : volume));
		return true;
	}

	bool AudioEngine::SetSoundPitch(SoundHandle handle, float pitch)
	{
		PlayingSound* entry = Find(handle);
		if (!entry)
			return false;

		// A pitch of zero would stall the stream; treat it as "leave it alone".
		if (pitch <= 0.0f)
			return false;

		ma_sound_set_pitch(entry->Sound, pitch);
		return true;
	}

	bool AudioEngine::IsSoundPlaying(SoundHandle handle)
	{
		PlayingSound* entry = Find(handle);
		if (!entry)
			return false;

		return ma_sound_is_playing(entry->Sound) != 0;
	}

	bool AudioEngine::IsValidSound(SoundHandle handle)
	{
		return Find(handle) != nullptr;
	}

	void AudioEngine::StopAllSounds()
	{
		// Copied handles first: StopSound mutates s_Sounds.
		std::vector<SoundHandle> handles;
		handles.reserve(s_Sounds.size());
		for (const auto& [handle, entry] : s_Sounds)
		{
			(void)entry;
			handles.push_back(handle);
		}

		for (SoundHandle handle : handles)
		{
			// Every handle came from s_Sounds, so this cannot fail; the cast
			// records that the result is intentionally discarded.
			(void)StopSound(handle);
		}
	}

	uint32_t AudioEngine::GetActiveSoundCount()
	{
		return static_cast<uint32_t>(s_Sounds.size());
	}

	bool AudioEngine::PlayAudioBuffer(const Ref<AudioBuffer>& buffer)
	{
		if (!buffer)
			return false;

		return PlaySound(buffer->GetSourcePath(), 1.0f, 1.0f, false, nullptr);
	}

	bool AudioEngine::StopAudioBuffer(const Ref<AudioBuffer>& buffer)
	{
		(void)buffer;

		// A decoded buffer carries no handle of its own, so there is nothing
		// individual to stop. Stops only the sounds whose source path matches.
		if (!s_Engine)
			return false;

		std::vector<SoundHandle> matches;
		if (buffer)
		{
			for (const auto& [handle, entry] : s_Sounds)
			{
				if (entry.SourcePath == buffer->GetSourcePath())
					matches.push_back(handle);
			}
		}

		if (matches.empty())
			return false;

		for (SoundHandle handle : matches)
		{
			(void)StopSound(handle);
		}

		return true;
	}

	void AudioEngine::SetMasterVolume(float volume)
	{
		s_MasterVolume = (volume < 0.0f) ? 0.0f : ((volume > 1.0f) ? 1.0f : volume);
		if (s_Engine)
			ma_engine_set_volume(s_Engine, s_MasterVolume);
	}

	float AudioEngine::GetMasterVolume()
	{
		return s_MasterVolume;
	}

}