#include "fgpch.h"
#include "Forge/Audio/AudioEngine.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace Forge {

	ma_engine* AudioEngine::s_Engine = nullptr;
	float AudioEngine::s_MasterVolume = 1.0f;

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
			FG_CORE_ERROR("AudioEngine: ma_engine_init failed ({0})", (int)result);
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

		auto buffer = CreateRef<AudioBuffer>();
		buffer->m_SourcePath = filepath;
		buffer->m_Samples = new (std::nothrow) float[(size_t)frameCount * 2];

		if (!buffer->m_Samples || frameCount == 0)
		{
			ma_decoder_uninit(&decoder);
			FG_CORE_ERROR("AudioEngine: empty/invalid audio '{0}'", filepath);
			return nullptr;
		}

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

	bool AudioEngine::PlayAudioBuffer(const Ref<AudioBuffer>& buffer)
	{
		if (!s_Engine || !buffer || buffer->GetSourcePath().empty())
			return false;

		ma_engine_set_volume(s_Engine, s_MasterVolume);
		// Fire-and-forget playback; the engine streams/decodes the file itself.
		ma_result result = ma_engine_play_sound(s_Engine, buffer->GetSourcePath().c_str(), nullptr);
		if (result != MA_SUCCESS)
		{
			// Expected when no output device exists (headless / CI).
			FG_CORE_TRACE("AudioEngine: playback unavailable ({0})", (int)result);
			return false;
		}

		return true;
	}

	bool AudioEngine::StopAudioBuffer(const Ref<AudioBuffer>& buffer)
	{
		// Without a dedicated sound handle we cannot stop an individual
		// fire-and-forget play; stopping the whole engine is the safe action.
		if (!s_Engine)
			return false;
		ma_engine_stop(s_Engine);
		(void)buffer;
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