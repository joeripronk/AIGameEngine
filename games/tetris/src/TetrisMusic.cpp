#include "fgpch.h"
#include "TetrisMusic.h"

#include "Forge/Core/Log.h"

#include <filesystem>
#include <system_error>

namespace Tetris {

namespace {

// The Music bus sits low against the block sounds the game will add later, and
// music under a HUD is background, not foreground.
constexpr float kThemeBusVolume = 0.35f;

// The engine's own mix rate, so the generated theme is played back at 1:1 and
// never resampled.
constexpr uint32_t kThemeSampleRate = 48000;

}  // namespace

std::string TetrisMusic::SongPath(const char* songId)
{
	return std::string("resources/audio/gen_") + songId + ".wav";
}

bool TetrisMusic::EnsureSongAsset(const ThemeSong& song, const std::string& path)
{
	std::error_code ec;
	if (std::filesystem::exists(path, ec))
		return true;

	const std::vector<float> pcm = RenderSongPcm(song, kThemeSampleRate);
	if (pcm.empty())
		return false;

	const Forge::Ref<Forge::AudioBuffer> buffer = Forge::AudioBuffer::FromSamples(
		pcm.data(), pcm.size() / 2, kThemeSampleRate, 2);
	if (!buffer)
		return false;

	const std::filesystem::path target(path);
	if (target.has_parent_path())
		std::filesystem::create_directories(target.parent_path(), ec);

	if (!buffer->WriteWav(path))
	{
		FG_WARN("Tetris: could not write '{0}'", path);
		return false;
	}

	FG_INFO("Tetris: rendered '{0}' to '{1}'", song.Title, path);
	return true;
}

bool TetrisMusic::Start()
{
	if (!Forge::AudioEngine::Init())
	{
		// Reported by the engine as a warning. Music is optional, exactly as
		// audio is optional in the engine: no device means a silent game, not
		// a failed one.
		FG_INFO("Tetris: continuing without music");
		return false;
	}

	// Build the playlist out of the authored files that are actually present.
	// A missing file is skipped and named, so a failure shows up as a line in
	// the log and a shorter playlist, not as a song that silently never plays.
	for (const ThemeTrack& track : ThemePlaylist())
	{
		std::error_code ec;
		if (track.Path == nullptr || !std::filesystem::exists(track.Path, ec))
		{
			FG_WARN("Tetris: music file '{0}' is missing; skipping it",
				track.Path != nullptr ? track.Path : track.Title);
			continue;
		}

		PlayableTrack playable;
		playable.Title = track.Title != nullptr ? track.Title : "";
		playable.Path = track.Path;
		m_Tracks.push_back(std::move(playable));
	}

	if (m_Tracks.empty())
	{
		FG_WARN("Tetris: no music available; playing without it");
		return false;
	}

	// A valid bus index cannot fail, so the result is deliberately dropped.
	(void)Forge::AudioEngine::SetBusVolume(Forge::AudioBus::Music, kThemeBusVolume);

	m_Ready = true;
	FG_INFO("Tetris: music ready, {0} songs", m_Tracks.size());
	return true;
}

void TetrisMusic::Update(const TetrisGameState& state)
{
	if (!m_Ready)
		return;

	const bool playing = m_Handle != Forge::AudioEngine::InvalidSound;

	if (ThemeIntentForState(state.CurrentState, m_Muted) == MusicIntent::Start)
	{
		if (!playing)
		{
			StartSong(state.Level);
		}
		else if (!Forge::AudioEngine::IsSoundPlaying(m_Handle))
		{
			// The song ended on its own: release it and move to the next one.
			// Advancing here, and not on a timer, is what keeps the music
			// continuous - the next song starts the frame the current one
			// finishes.
			(void)Forge::AudioEngine::StopSound(m_Handle);
			m_Handle = Forge::AudioEngine::InvalidSound;
			m_TrackIndex = static_cast<size_t>(NextTrackIndex(
				static_cast<int>(m_TrackIndex), static_cast<int>(m_Tracks.size()), +1));
			StartSong(state.Level);
		}
		else if (state.Level != m_LastLevel)
		{
			// The tempo follows the level: the music accelerates as the well
			// fills, which is the one thing that makes Tetris music feel like
			// it belongs to the game.
			if (Forge::AudioEngine::SetSoundPitch(m_Handle, ThemePitchForLevel(state.Level)))
				m_LastLevel = state.Level;
		}
	}
	else if (playing)
	{
		// StopSound fades out and the handle stays live until the fade has
		// finished, so the handle is dropped here and AudioEngine::Update
		// below reclaims it; stopping it outright would cut the decay off,
		// which is the click the fade exists to avoid.
		if (Forge::AudioEngine::StopSound(m_Handle))
			m_Handle = Forge::AudioEngine::InvalidSound;
	}

	Forge::AudioEngine::Update();
}

void TetrisMusic::SkipTrack(int delta)
{
	if (m_Tracks.empty() || delta == 0)
		return;

	m_TrackIndex = static_cast<size_t>(NextTrackIndex(
		static_cast<int>(m_TrackIndex), static_cast<int>(m_Tracks.size()), delta));

	// Skipping a playing song restarts on the chosen one: the fade-out of the
	// song being left overlaps the new one, which is the smoothest thing
	// available without a crossfade. Stopped music just records the choice,
	// and the next Start plays that song.
	if (m_Handle != Forge::AudioEngine::InvalidSound)
	{
		(void)Forge::AudioEngine::StopSound(m_Handle);
		m_Handle = Forge::AudioEngine::InvalidSound;
		StartSong(m_LastLevel);
	}
}

const char* TetrisMusic::CurrentTitle() const
{
	if (m_Tracks.empty() || m_TrackIndex >= m_Tracks.size())
		return "";

	return m_Tracks[m_TrackIndex].Title.c_str();
}

void TetrisMusic::StartSong(int32_t level)
{
	if (m_Tracks.empty())
		return;

	Forge::AudioEngine::SoundHandle handle = Forge::AudioEngine::InvalidSound;

	// One-shot, not looping: the playlist is the loop. The bus is chosen at
	// start, not switched afterwards, so the first sample is already mixed
	// through Music.
	const bool started = Forge::AudioEngine::PlaySoundOnBus(
		m_Tracks[m_TrackIndex].Path, Forge::AudioBus::Music, 1.0f,
		ThemePitchForLevel(level), false, &handle);

	if (!started)
	{
		// Say it once, not once per frame: an unreadable file would otherwise
		// print a warning on every frame of the game.
		if (!m_WarnedMissing)
		{
			FG_WARN("Tetris: could not play '{0}'; continuing without music",
				m_Tracks[m_TrackIndex].Path.c_str());
			m_WarnedMissing = true;
			m_Ready = false;
		}
		return;
	}

	m_Handle = handle;
	m_LastLevel = level;
}

void TetrisMusic::Shutdown()
{
	if (m_Handle != Forge::AudioEngine::InvalidSound)
	{
		(void)Forge::AudioEngine::StopSound(m_Handle);
		m_Handle = Forge::AudioEngine::InvalidSound;
	}

	m_Ready = false;
	m_Tracks.clear();

	// The app is the only audio user in this game, so it is also the one that
	// closes the engine down.
	Forge::AudioEngine::Shutdown();
}

}  // namespace Tetris
