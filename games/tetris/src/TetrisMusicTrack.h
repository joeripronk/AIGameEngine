#pragma once

#include "TetrisTypes.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Tetris {

// The generated song: its data and its maths, with no engine in sight.
//
// This is a small arranger and synthesiser, not a recording. It writes the
// melody, the drums and the mix as arithmetic on PCM, so what it produces can
// be checked by a test with no window and no sound card - and so a mistake in
// the arrangement is a failing assertion rather than something the player has
// to hear first.
//
// The arrangement follows the three songs in resources/audio that the game
// plays (Block Drop, Block Stacker, Falling Blocks): a minor key, 132 BPM, a
// sub-heavy four-on-the-floor-adjacent groove with an offbeat-weighted kick,
// backbeat snare, offbeat hats, a quiet opening, a dip, a bigger finish, and
// side-chain ducking under the kick. What is borrowed is the arrangement and
// the drum grammar, not any of their notes.

// One melody note, measured in eighth-note units - four units to a bar.
// A Midi of 0 is a rest, and a rest at the end of the tune is what makes the
// loop wrap in silence rather than on a note.
struct MusicNote
{
	int16_t Midi;
	float Units;
};

// "Korobeiniki", the 19th-century Russian folk song the Tetris theme comes
// from. The melody is public domain, and this is the folk ("Korobushka")
// transcription in D minor as published in Jack Campin's Nine-Note Tunebook -
// not a transcription of any game's arrangement of it. Sixteen bars.
const std::vector<MusicNote>& KorobeinikiMelody();

// The root of the bass in each bar of the melody, and the chord built on it:
// minor on D and G, major on A, which is what the melody's own phrasing asks
// for (i, iv, V).
const std::vector<int16_t>& ThemeBassRoots();

// Seconds per eighth-note unit. 132 BPM: the reference songs sit between 125
// and 148, and this is where a Tetris loop sits between them.
float ThemeEighthSeconds();

// One bar, in seconds.
float ThemeBarSeconds();

// Bars in the whole form, including the intro and outro that hold the loop's
// edges still.
uint32_t ThemeBarCount();

// Length of one pass of the song, in seconds.
float ThemeLoopSeconds();

// A drum step: one slot of a bar, a velocity in [0, 1]. Zero is silence, so a
// pattern reads as a bar of music when printed.
using DrumPattern = std::vector<float>;

// One part of the form. The sections tile the bar range from 0 to the bar
// count with no gaps and no overlaps, which is what keeps the loop point where
// the arranger thinks it is.
struct ThemeSection
{
	const char* Name;
	uint32_t FirstBar;
	uint32_t BarCount;
};

// The generated song, as interleaved stereo PCM at `sampleRate`: exactly
// ThemeLoopSeconds() long, starting and ending in silence so the loop wraps
// without a seam.
std::vector<float> RenderThemePcm(uint32_t sampleRate);

// --- Songs ---
//
// A song is data, not code: a tempo, a meter, a bar count, a form, a chord per
// bar and the notes the lead sings. The renderer is one function over that
// description, so a song is added by writing a table rather than by writing
// more synthesis. The Korobeiniki theme and the three songs reconstructed from
// the authored tracks are all the same shape.

// One chord, as the two notes the pad and the bass need.
struct ThemeChord
{
	int16_t Root;   // bass root, MIDI note
	int16_t Third;  // pad's third, MIDI note
};

// One note the lead sings, placed at a bar and a position within it.
struct ThemeMelodyNote
{
	uint32_t Bar;
	int16_t Midi;
	uint8_t OffsetEighths;   // where in the bar, in eighth notes
	uint8_t DurationEighths; // how long, in eighth notes
};

// A song's balance, as multipliers on the shared voice levels. The voices are
// one set of gains for every song, but the songs do not want the same mix: a
// track that lives in the mids needs its lead up and its sub down, and these
// are what carry that.
struct ThemeMix
{
	float Kick = 1.0f;
	float Snare = 1.0f;
	float Hat = 1.0f;
	float Bass = 1.0f;
	float Pad = 1.0f;
	float Lead = 1.0f;
	float Arp = 1.0f;
};

struct ThemeSong
{
	const char* Id;
	const char* Title;

	// Tempo and meter. A 2/4 theme has four eighths to a bar, a reconstructed
	// 4/4 track eight; the renderer reads both off the song rather than
	// assuming the one the theme happens to use.
	float EighthSeconds;
	uint8_t EighthsPerBar;
	uint32_t BarCount;

	// The form, and the harmony under it: one chord per bar.
	std::vector<ThemeSection> Sections;
	std::vector<ThemeChord> Chords;

	// The lead line, and the groove it plays over.
	std::vector<ThemeMelodyNote> Melody;
	DrumPattern Kick;
	DrumPattern Snare;
	DrumPattern Hat;

	// How loud the arrangement is, so a song can sit differently in the mix.
	float ArrangementGain = 1.0f;

	// Per-voice balance; see ThemeMix.
	ThemeMix Mix;
};

// The songs the game can generate: the Korobeiniki theme first, then the ones
// reconstructed from the authored tracks. Returns an empty vector never.
const std::vector<ThemeSong>& GeneratedSongs();

// The song at `index`, clamped to the last song, so a stale index cannot
// produce an empty song.
const ThemeSong& GeneratedSong(size_t index);

// Renders any song as interleaved stereo PCM. RenderThemePcm is this over the
// Korobeiniki song; the tests use it to check the songs are playable and that
// their loop seams are silent.
std::vector<float> RenderSongPcm(const ThemeSong& song, uint32_t sampleRate);

// One bar of `song`, in seconds.
float SongBarSeconds(const ThemeSong& song);

// One loop of `song`, in seconds.
float SongLoopSeconds(const ThemeSong& song);

// One loop of `song`, in frames at `sampleRate`. The renderer rounds seconds to
// the nearest frame, so anyone measuring the rendered PCM has to use this and
// not their own `seconds * rate` cast: two roundings of one number are the
// classic way to be off by a frame, and a loop that is a frame longer than the
// seam the test checked is a seam.
size_t SongLoopFrames(const ThemeSong& song, uint32_t sampleRate);

// --- The arrangement, as data ---
//
// Exposed so a test can assert the shape of the form - that the sections tile
// the bars exactly, that every section is named, that the drums are on all
// sixteen steps - without decoding the PCM and guessing.

const DrumPattern& ThemeKickPattern();
const DrumPattern& ThemeSnarePattern();
const DrumPattern& ThemeHatPattern();

const std::vector<ThemeSection>& ThemeArrangement();

// --- Playing the song ---

// Playback speed for a level. The song runs faster as the well fills up, so
// the music accelerates with the game. Never below 1, never above the cap, and
// never decreasing as the level rises.
float ThemePitchForLevel(int32_t level);

// What the music should be doing for a game state, given the player's mute
// switch. Pure so the state machine is testable with no audio device: the
// headless case is exactly the case a test can assert on.
enum class MusicIntent : uint8_t
{
	Start,
	Stop
};

MusicIntent ThemeIntentForState(TetrisGameState::State state, bool muted);

// --- The playlist ---

// One entry of the background-music playlist.
//
// Either a file to play (Path) or a song to generate (SongId); exactly one is
// set. The generated entries are the arrangements the game synthesises on
// first run, so the playlist ships no audio of its own.
struct ThemeTrack
{
	const char* Title;
	const char* Path;    // null when the song is generated
	const char* SongId;  // null when the track is a file
};

// The playlist, in play order: the three authored tracks in resources/audio.
// The generated arrangements are not in it - they measured worse than the
// recordings they were reconstructed from - so the game plays the originals.
const std::vector<ThemeTrack>& ThemePlaylist();

// The playlist index `delta` songs along from `index`, wrapping in both
// directions. `count` of zero gives zero: there is nothing to advance through.
// Pure, so the wrap - the part of a playlist that is easiest to get wrong at
// the ends - is assertable without a game.
int NextTrackIndex(int index, int count, int delta);

}  // namespace Tetris