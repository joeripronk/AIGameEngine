#pragma once

#include "TetrisMusicTrack.h"
#include "TetrisTypes.h"

#include "Forge/Audio/AudioEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Tetris {

// The game's background music: the playlist from TetrisMusicTrack, played on
// the Music bus and driven by the game state.
//
// Music is presentation, not a rule - TetrisManager never hears it - so this
// reads the manager's state each frame and decides what the music is doing.
// Everything that decision depends on (ThemeIntentForState, ThemePitchForLevel,
// NextTrackIndex) is pure and lives in TetrisMusicTrack, where the tests can
// reach it; what is left here is the handful of AudioEngine calls that make it
// audible.
//
// Each song plays to its end and the next one starts, wrapping at the end of
// the list, so the music runs continuously while the game does. N and B move
// to another song; M mutes.
//
// Audio is optional in the engine: with no output device every call below
// fails quietly and the game plays silent, which is the correct behaviour on a
// machine with no sound card, and in CI.
class TetrisMusic
{
public:
	// Where a generated song is written, next to the block model the game
	// already reads from its working directory. Not used by the playlist (the
	// authored files play); public and static so a test can render a song to
	// a temporary directory and decode the result back with no audio device.
	static std::string SongPath(const char* songId);

	// Renders `song` to its file if the file is not already there. Public and
	// static so a test can render a song to a temporary directory and decode
	// the result back with no audio device.
	[[nodiscard]] static bool EnsureSongAsset(const ThemeSong& song, const std::string& path);

	// Prepare audio: initialise the engine and build the playlist out of the
	// authored files that are present. Returns false when there is no audio
	// device or no playable file; the game continues silently either way.
	bool Start();

	// Per-frame: apply the state's intent, advance the playlist when a song
	// ends, follow the level with the tempo, and let the engine reclaim
	// sounds whose fade-out has finished. The Tetris app drives its scene in
	// code and never calls the Scene's runtime update, so this is where
	// AudioEngine::Update gets called.
	void Update(const TetrisGameState& state);

	// The player's mute switch (M). Muting silences the music without
	// touching the game state.
	void SetMuted(bool muted) { m_Muted = muted; }
	bool IsMuted() const { return m_Muted; }

	// Move through the playlist by `delta` songs (N is +1, B is -1), wrapping.
	// Playing music restarts on the chosen song immediately; stopped music
	// starts on it the next time the game plays.
	void SkipTrack(int delta);

	// The song the player is on, for the HUD. Empty when there is no music.
	const char* CurrentTitle() const;
	int CurrentTrackIndex() const { return static_cast<int>(m_TrackIndex); }
	int TrackCount() const { return static_cast<int>(m_Tracks.size()); }

	// Stop the music and shut the audio engine down.
	void Shutdown();

private:
	// Start the current song at the given level's tempo.
	void StartSong(int32_t level);

	// One playable entry: the file to play.
	struct PlayableTrack
	{
		std::string Title;
		std::string Path;
	};

	std::vector<PlayableTrack> m_Tracks;
	size_t m_TrackIndex = 0;

	Forge::AudioEngine::SoundHandle m_Handle = Forge::AudioEngine::InvalidSound;
	bool m_Ready = false;
	bool m_Muted = false;
	bool m_WarnedMissing = false;
	int32_t m_LastLevel = 0;
};

}  // namespace Tetris
