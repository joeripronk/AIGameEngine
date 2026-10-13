#include "fgpch.h"
#include "Forge/Audio/AudioEngine.h"

namespace Forge {

	void* AudioEngine::s_AudioContext = nullptr;

	void AudioEngine::Init()
	{
		// Initialize miniaudio context
		// ma_context_config config = ma_context_config_init();
		// ma_context_init(&config, 0, &s_AudioContext);
		FG_CORE_INFO("AudioEngine initialized");
	}

	void AudioEngine::Shutdown()
	{
		// ma_context_uninit(s_AudioContext);
		FG_CORE_INFO("AudioEngine shut down");
	}

	Ref<class AudioBuffer> AudioEngine::LoadAudioBuffer(const std::string& filepath)
	{
		// TODO: Load audio buffer using miniaudio
		return nullptr;
	}

	void AudioEngine::PlayAudioBuffer(Ref<class AudioBuffer> buffer)
	{
		// TODO: Play audio buffer
	}

	void AudioEngine::StopAudioBuffer(Ref<class AudioBuffer> buffer)
	{
		// TODO: Stop audio buffer
	}

}
