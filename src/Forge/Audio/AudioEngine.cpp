#include "fgpch.h"
#include "Forge/Audio/AudioEngine.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <algorithm>
#include <cmath>

namespace Forge {

	ma_engine* AudioEngine::s_Engine = nullptr;
	float AudioEngine::s_MasterVolume = 1.0f;
	AudioEngine::ListenerState AudioEngine::s_Listener{};
	float AudioEngine::s_FadeDuration = 0.02f;
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
		return StartSound(filepath, volume, pitch, loop, nullptr, SpatialParams{}, outHandle);
	}

	bool AudioEngine::PlaySoundAt(const std::string& filepath, const glm::vec3& position,
		const SpatialParams& spatial, float volume, float pitch, bool loop,
		SoundHandle* outHandle)
	{
		return StartSound(filepath, volume, pitch, loop, &position, spatial, outHandle);
	}

	bool AudioEngine::StartSound(const std::string& filepath, float volume, float pitch, bool loop,
		const glm::vec3* position, const SpatialParams& spatial, SoundHandle* outHandle)
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

		// Ramp volume changes instead of stepping them, which is what removes
		// the click when a source's volume or spatial gain changes mid-playback.
		config.volumeSmoothTimeInPCMFrames = static_cast<ma_uint32>(FadeFrames());

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

		ma_sound_set_pitch(sound, (pitch <= 0.0f) ? 1.0f : pitch);
		ma_sound_set_looping(sound, loop ? 1 : 0);

		const SoundHandle handle = s_NextHandle++;
		PlayingSound& entry = s_Sounds[handle];
		entry.Sound = sound;
		entry.SourcePath = filepath;
		entry.BaseVolume = (volume < 0.0f) ? 0.0f : ((volume > 1.0f) ? 1.0f : volume);
		entry.Spatial = spatial;
		entry.Position = position ? *position : glm::vec3(0.0f);

		// A spatial sound starts at its computed mix, not its authored volume.
		if (spatial.Enabled)
		{
			const SpatialMix mix = ComputeSpatialMix(s_Listener, entry.Position, spatial);
			ma_sound_set_volume(sound, entry.BaseVolume * mix.Gain);
			ma_sound_set_pan(sound, mix.Pan);
		}
		else
		{
			ma_sound_set_volume(sound, entry.BaseVolume);
		}

		if (outHandle)
			*outHandle = handle;

		// Fade in from silence so the start is not a discontinuity.
		const ma_uint64 fadeFrames = FadeFrames();
		if (fadeFrames > 0)
			ma_sound_set_fade_in_pcm_frames(sound, 0.0f, entry.BaseVolume, fadeFrames);

		return true;
	}

	// --- Click-free playback ----------------------------------------------

	void AudioEngine::SetFadeDuration(float seconds)
	{
		s_FadeDuration = (seconds < 0.0f) ? 0.0f : seconds;
	}

	float AudioEngine::GetFadeDuration()
	{
		return s_FadeDuration;
	}

	// The fade length in PCM frames, or 0 when fading is disabled or there is no
	// engine to ask for a sample rate.
	uint64_t AudioEngine::FadeFrames()
	{
		if (!s_Engine || s_FadeDuration <= 0.0f)
			return 0;

		const ma_uint32 sampleRate = ma_engine_get_sample_rate(s_Engine);
		if (sampleRate == 0)
			return 0;

		return static_cast<ma_uint64>(s_FadeDuration * sampleRate);
	}

	void AudioEngine::Update()
	{
		// Reclaim sounds whose fade-out has finished. Collected first because
		// releasing mutates the map.
		std::vector<SoundHandle> finished;
		for (const auto& [handle, entry] : s_Sounds)
		{
			if (!entry.PendingRelease)
				continue;

			// A sound stopped with no fade has already stopped playing.
			if (ma_sound_is_playing(entry.Sound) == 0)
				finished.push_back(handle);
		}

		for (SoundHandle handle : finished)
			ReleaseSound(handle);

		UpdateSpatialization();
	}

	void AudioEngine::ReleaseSound(SoundHandle handle)
	{
		auto it = s_Sounds.find(handle);
		if (it == s_Sounds.end())
			return;

		if (it->second.Sound)
		{
			ma_sound_uninit(it->second.Sound);
			delete it->second.Sound;
		}

		s_Sounds.erase(it);
	}

	// --- 3D audio ---------------------------------------------------------

	void AudioEngine::SetListener(const AudioEngine::ListenerState& listener)
	{
		s_Listener = listener;

		// Degenerate bases would produce NaN in ComputeSpatialMix; fall back to a
		// sane default orientation rather than poisoning every sound.
		if (glm::length(s_Listener.Forward) < 1e-6f)
			s_Listener.Forward = { 0.0f, 0.0f, -1.0f };

		if (glm::length(s_Listener.Up) < 1e-6f)
			s_Listener.Up = { 0.0f, 1.0f, 0.0f };
	}

	const AudioEngine::ListenerState& AudioEngine::GetListener()
	{
		return s_Listener;
	}

	bool AudioEngine::SetSoundPosition(SoundHandle handle, const glm::vec3& position)
	{
		PlayingSound* entry = Find(handle);
		if (!entry)
			return false;

		entry->Position = position;
		return true;
	}

	AudioEngine::SpatialMix AudioEngine::ComputeSpatialMix(const ListenerState& listener,
		const glm::vec3& source, const SpatialParams& spatial)
	{
		SpatialMix mix;

		if (!spatial.Enabled)
			return mix;   // full volume, centred

		const glm::vec3 toSource = source - listener.Position;
		const float distance = glm::length(toSource);

		// At or inside MinDistance the sound is at full volume. Beyond
		// MaxDistance it is inaudible.
		const float minDistance = std::max(spatial.MinDistance, 0.0f);
		const float maxDistance = std::max(spatial.MaxDistance, minDistance + 1e-4f);

		if (distance >= maxDistance)
			mix.Gain = 0.0f;
		else if (distance <= minDistance)
			mix.Gain = 1.0f;
		else
		{
			const float normalized = (distance - minDistance) / (maxDistance - minDistance);
			const float falloff = 1.0f - glm::clamp(normalized, 0.0f, 1.0f);

			// Applying rolloff as an exponent keeps 1.0 meaning "linear", which
			// is the intuitive default.
			mix.Gain = std::pow(falloff, std::max(spatial.Rolloff, 0.0f));
		}

		// A sound at (or extremely near) the listener has no meaningful
		// direction, so leave it centred rather than dividing by ~0.
		if (distance < 1e-4f)
			return mix;

		// Pan from the source's offset along the listener's right axis. Using the
		// right vector rather than the full direction keeps a sound directly
		// behind the listener centred, which is what a listener expects.
		const glm::vec3 forward = glm::normalize(listener.Forward);
		const glm::vec3 right = glm::cross(forward, glm::normalize(listener.Up));

		const glm::vec3 direction = toSource / distance;
		mix.Pan = glm::clamp(dot(direction, right), -1.0f, 1.0f);

		return mix;
	}

	void AudioEngine::UpdateSpatialization()
	{
		for (auto& [handle, entry] : s_Sounds)
		{
			(void)handle;

			if (!entry.Spatial.Enabled || !entry.Sound)
				continue;

			const SpatialMix mix = ComputeSpatialMix(s_Listener, entry.Position, entry.Spatial);
			ma_sound_set_volume(entry.Sound, entry.BaseVolume * mix.Gain);
			ma_sound_set_pan(entry.Sound, mix.Pan);
		}
	}

	bool AudioEngine::StopSound(SoundHandle handle)
	{
		PlayingSound* entry = Find(handle);
		if (!entry)
			return false;

		// Already fading out; a second stop is not an error but has nothing to
		// do, so report failure the same way an unknown handle would.
		if (entry->PendingRelease)
			return false;

		const ma_uint64 fadeFrames = FadeFrames();
		if (fadeFrames > 0 && ma_sound_is_playing(entry->Sound) != 0)
		{
			// Keep the sound alive for the duration of the fade, then reclaim it
			// on the next Update(). Releasing it now would cut the sound off
			// mid-decay, which is exactly the click we are avoiding.
			ma_sound_stop_with_fade_in_pcm_frames(entry->Sound, fadeFrames);
			entry->PendingRelease = true;
			return true;
		}

		// Nothing playing or fading disabled: release immediately.
		ReleaseSound(handle);
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
		// Copied handles first: releasing mutates s_Sounds.
		std::vector<SoundHandle> handles;
		handles.reserve(s_Sounds.size());
		for (const auto& [handle, entry] : s_Sounds)
		{
			(void)entry;
			handles.push_back(handle);
		}

		// Released outright rather than faded: this is teardown (shutdown, or
		// stopping playback), and leaving sounds alive for a fade would let them
		// keep playing after the scene that owns them is gone.
		for (SoundHandle handle : handles)
			ReleaseSound(handle);
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