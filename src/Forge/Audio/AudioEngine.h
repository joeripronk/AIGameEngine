#pragma once

#include "Forge/Core/Base.h"

namespace Forge {

	class AudioEngine
	{
	public:
		static void Init();
		static void Shutdown();

		static Ref<class AudioBuffer> LoadAudioBuffer(const std::string& filepath);
		static void PlayAudioBuffer(Ref<class AudioBuffer> buffer);
		static void StopAudioBuffer(Ref<class AudioBuffer> buffer);
	private:
		static void* s_AudioContext;
	};

}
