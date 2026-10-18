#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "TetrisMusic.h"
#include "TetrisMusicTrack.h"
#include "TetrisTypes.h"

#include "Forge/Audio/AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace Tetris;

namespace {

// Scratch for the bake test. Relative, because the test target's working
// directory is the game's directory, which keeps the artefact out of /tmp and
// next to the run that made it.
const std::filesystem::path kScratch = "test_tetris_music";

struct ScratchGuard
{
	ScratchGuard()
	{
		std::error_code ec;
		std::filesystem::remove_all(kScratch, ec);
		std::filesystem::create_directories(kScratch, ec);
	}

	~ScratchGuard()
	{
		std::error_code ec;
		std::filesystem::remove_all(kScratch, ec);
	}
};

float PeakAbs(const std::vector<float>& samples)
{
	float peak = 0.0f;
	for (const float sample : samples)
		peak = std::max(peak, std::abs(sample));
	return peak;
}

float RmsAbs(const std::vector<float>& samples, size_t begin, size_t end)
{
	double sum = 0.0;
	size_t count = 0;
	for (size_t i = begin; i < end && i < samples.size(); ++i)
	{
		sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
		++count;
	}
	return count == 0 ? 0.0f : static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

// RMS over a wall-clock window, on the mono sum. Used to compare sections.
float RmsWindow(const std::vector<float>& samples, uint32_t rate, float fromSeconds, float toSeconds)
{
	const size_t begin = static_cast<size_t>(fromSeconds * rate) * 2;
	const size_t end = static_cast<size_t>(toSeconds * rate) * 2;
	return RmsAbs(samples, begin, end);
}

}  // namespace

// ---------------------------------------------------------------------------
// The tune itself
// ---------------------------------------------------------------------------

TEST_CASE("The theme is sixteen bars of four eighth units, ending in silence", "[tetris][music]")
{
	const std::vector<MusicNote>& melody = KorobeinikiMelody();

	REQUIRE(melody.size() >= 16);

	float units = 0.0f;
	for (const MusicNote& note : melody)
	{
		// A zero-length note is a data bug: it advances nothing and the loop
		// maths silently counts it as bar content.
		REQUIRE(note.Units > 0.0f);
		REQUIRE(note.Midi >= 0);
		units += note.Units;
	}

	// 16 bars x 4 units, and the last unit of the tune is the rest that makes
	// the loop wrap in silence.
	REQUIRE(units == Approx(64.0f).margin(0.001f));
	REQUIRE(melody.back().Midi == 0);

	// The tune starts on the tonic, which is what a loop has to land on.
	REQUIRE(melody.front().Midi == 69);
}

TEST_CASE("The melody stays inside the range the synthesiser was written for", "[tetris][music]")
{
	// The folk transcription is D4..D5 (62..74). Outside it the bass and the
	// harmonic budget below Nyquist no longer describe the wave being built.
	for (const MusicNote& note : KorobeinikiMelody())
	{
		if (note.Midi == 0)
			continue;
		INFO("midi " << note.Midi);
		REQUIRE(note.Midi >= 62);
		REQUIRE(note.Midi <= 74);
	}
}

TEST_CASE("The song is a 132 BPM form with a loop length the bars account for", "[tetris][music]")
{
	// 132 BPM: an eighth is 60/132/2 seconds. The three songs the arrangement
	// borrows from run 125, 148 and 128, so this has to stay inside that band
	// or the reference is not a reference.
	REQUIRE(ThemeEighthSeconds() == Approx(60.0f / 132.0f / 2.0f));
	REQUIRE(ThemeBarSeconds() == Approx(4.0f * ThemeEighthSeconds()));

	// The loop length is the bar count times the bar, or the seam maths and
	// the buffer length disagree by a fraction of a frame.
	REQUIRE(ThemeLoopSeconds() == Approx(static_cast<float>(ThemeBarCount()) * ThemeBarSeconds()).margin(0.001f));
	REQUIRE(ThemeBarCount() >= 16);
}

TEST_CASE("The sections tile the form with no gap and no overlap", "[tetris][music]")
{
	const std::vector<ThemeSection>& sections = ThemeArrangement();
	REQUIRE_FALSE(sections.empty());

	// The arranger looks up a bar's section by scanning for containment, so
	// a gap or an overlap means a bar with no section - the melody's first bar
	// would be dropped on the floor without anything failing.
	uint32_t expectedFirstBar = 0;
	for (const ThemeSection& section : sections)
	{
		INFO("section " << section.Name);
		REQUIRE(section.Name != nullptr);
		REQUIRE(*section.Name != '\0');
		REQUIRE(section.BarCount > 0);
		REQUIRE(section.FirstBar == expectedFirstBar);

		expectedFirstBar = section.FirstBar + section.BarCount;
	}

	// And they cover the whole form, ending exactly on the last bar.
	REQUIRE(expectedFirstBar == ThemeBarCount());
}

TEST_CASE("The drums are a 2/4 bar of real velocities", "[tetris][music]")
{
	const DrumPattern* patterns[] = { &ThemeKickPattern(), &ThemeSnarePattern(), &ThemeHatPattern() };
	const char* names[] = { "kick", "snare", "hat" };

	for (int i = 0; i < 3; ++i)
	{
		const DrumPattern& pattern = *patterns[i];
		INFO(names[i]);

		// The tune is in 2/4, so a bar is eight sixteenths. A sixteen-slot
		// pattern would put every hit a half-bar out of phase with the
		// melody, which is a groove that fights the music.
		REQUIRE(pattern.size() == 8);

		float loudest = 0.0f;
		for (const float velocity : pattern)
		{
			// A velocity outside [0, 1] is either inaudible or a click.
			REQUIRE(velocity >= 0.0f);
			REQUIRE(velocity <= 1.0f);
			loudest = std::max(loudest, velocity);
		}

		// A drum that never plays is a missing drum.
		REQUIRE(loudest > 0.5f);
	}
}

TEST_CASE("The kick lands on the beat and the snare on the backbeat", "[tetris][music]")
{
	const DrumPattern& kick = ThemeKickPattern();
	const DrumPattern& snare = ThemeSnarePattern();

	// Both beats of the 2/4 bar, which is the spine all three reference songs
	// are built on.
	REQUIRE(kick[0] > 0.9f);
	REQUIRE(kick[4] > 0.9f);

	// Backbeat on the second beat, with nothing on the first: a snare on one
	// and four is the sound, and a snare on one as well is a stumble.
	REQUIRE(snare[4] > 0.9f);
	REQUIRE(snare[0] == 0.0f);
}

TEST_CASE("The tempo rises with the level and stops rising at the cap", "[tetris][music]")
{
	REQUIRE(ThemePitchForLevel(1) == Approx(1.0f));
	REQUIRE(ThemePitchForLevel(0) == Approx(1.0f));

	float previous = ThemePitchForLevel(1);
	for (int32_t level = 2; level <= 40; ++level)
	{
		const float pitch = ThemePitchForLevel(level);

		// Monotonic, and never below unity: a music pitch below 1 would slow
		// the tune down as the game got harder.
		INFO("level " << level);
		REQUIRE(pitch >= previous);
		REQUIRE(pitch >= 1.0f);
		REQUIRE(pitch <= 1.45f);

		previous = pitch;
	}

	// The cap is reached, not approached: past it the tune is a chipmunk.
	REQUIRE(ThemePitchForLevel(40) == Approx(1.45f));
}

TEST_CASE("The music plays only while the game is playing", "[tetris][music]")
{
	// The menu is silent, a pause is silent, a dead game is silent.
	REQUIRE(ThemeIntentForState(TetrisGameState::State::Playing, false) == MusicIntent::Start);
	REQUIRE(ThemeIntentForState(TetrisGameState::State::Menu, false) == MusicIntent::Stop);
	REQUIRE(ThemeIntentForState(TetrisGameState::State::Paused, false) == MusicIntent::Stop);
	REQUIRE(ThemeIntentForState(TetrisGameState::State::GameOver, false) == MusicIntent::Stop);

	// The mute switch outranks the state: playing and muted is silence.
	REQUIRE(ThemeIntentForState(TetrisGameState::State::Playing, true) == MusicIntent::Stop);
}

// ---------------------------------------------------------------------------
// The synthesised PCM
// ---------------------------------------------------------------------------

TEST_CASE("The rendered theme is a full-length, in-range, non-silent loop", "[tetris][music]")
{
	const uint32_t rate = 48000;
	const std::vector<float> pcm = RenderThemePcm(rate);

	const size_t expectedFrames = static_cast<size_t>(ThemeLoopSeconds() * rate);
	REQUIRE(pcm.size() == expectedFrames * 2);

	// Nothing may clip: WriteWav clamps, and a clamped sample is a square
	// edge in the file, which is a crackle every time the peak is hit.
	REQUIRE(PeakAbs(pcm) <= 1.0f);

	// Not silent, and not near-silent either.
	REQUIRE(PeakAbs(pcm) > 0.30f);
	REQUIRE(RmsAbs(pcm, 0, pcm.size()) > 0.05f);

	// Both channels carry the tune: the lead is centre and the wide copies
	// are hard-panned, so a channel that came out empty means the panning law
	// is wrong.
	REQUIRE(RmsAbs(pcm, 0, pcm.size() / 2) > 0.05f);
	REQUIRE(RmsAbs(pcm, pcm.size() / 2, pcm.size()) > 0.05f);

	// The form has an arc: the intro ticks and pads, the chorus has the kick,
	// the bass and the arp over it, so the chorus is measurably louder than
	// the intro. A generated song with a flat envelope is a loop, not a
	// song, and this is the assertion that says so.
	const float introEnergy = RmsWindow(pcm, rate, 1.0f, 2.0f);
	const float chorusEnergy = RmsWindow(pcm, rate,
		ThemeBarSeconds() * 14.0f, ThemeBarSeconds() * 15.0f);
	INFO("intro " << introEnergy << " chorus " << chorusEnergy);
	REQUIRE(chorusEnergy > introEnergy * 1.4f);

	// The bridge drops the drums, so it is quieter than the chorus that
	// precedes it - the dip is the shape.
	const float bridgeEnergy = RmsWindow(pcm, rate,
		ThemeBarSeconds() * 21.0f, ThemeBarSeconds() * 22.0f);
	INFO("bridge " << bridgeEnergy);
	REQUIRE(bridgeEnergy < chorusEnergy * 0.9f);

	// The loop seam: every note ends on a release ramp, and the last hundred
	// milliseconds are silence, so the wrap joins a note to a rest rather than
	// a sample to a different sample. That is the click between loops that
	// looping audio is notorious for, and it is what the written rest at the
	// end of the tune is for.
	const size_t tailFrames = rate / 10;
	REQUIRE(RmsAbs(pcm, (expectedFrames - tailFrames) * 2, pcm.size()) < 0.001f);
	REQUIRE(std::abs(pcm.back()) < 0.001f);

	// And it leaves the loop point at zero, so the wrap is silence meeting the
	// first bar's opening kick rather than two unrelated waveforms butted
	// together. A kick starts a fraction of a cycle in, so the first sample is
	// small rather than exactly zero; what matters is that the step across the
	// wrap is far below anything audible, and that the song does not resume on
	// a loud sample.
	REQUIRE(std::abs(pcm.front()) < 0.05f);
	REQUIRE(std::abs(pcm.front() - pcm.back()) < 0.01f);
}

// ---------------------------------------------------------------------------
// The playlist
// ---------------------------------------------------------------------------

TEST_CASE("The playlist is a playable set of named songs", "[tetris][music]")
{
	const std::vector<ThemeTrack>& playlist = ThemePlaylist();

	REQUIRE(playlist.size() >= 2);

	std::vector<std::string> paths;
	for (const ThemeTrack& track : playlist)
	{
		INFO("path " << track.Path);

		// A song with no title is a song the HUD cannot show, which makes the
		// skip keys look dead.
		REQUIRE(track.Title != nullptr);
		REQUIRE(*track.Title != '\0');

		// Every entry is a file in the game's audio tree, with an extension
		// the engine's decoder handles.
		const std::filesystem::path file(track.Path);
		REQUIRE(file.parent_path() == std::filesystem::path("resources/audio"));
		const std::string extension = file.extension().string();
		REQUIRE((extension == ".mp3" || extension == ".ogg" || extension == ".wav" || extension == ".flac"));

		// The same song twice would play twice per cycle and skip to a song
		// the player already heard.
		REQUIRE(std::find(paths.begin(), paths.end(), std::string(track.Path)) == paths.end());
		paths.emplace_back(track.Path);

		// The playlist plays authored files, not generated songs.
		REQUIRE(track.SongId == nullptr);
	}
}

TEST_CASE("Skipping wraps the playlist at both ends", "[tetris][music]")
{
	// Forward off the end comes back to the first song.
	REQUIRE(NextTrackIndex(3, 4, +1) == 0);
	REQUIRE(NextTrackIndex(0, 4, +1) == 1);

	// Backwards off the start lands on the last song. This is the case C++'s
	// sign-preserving modulo gets wrong, and a negative index is an
	// out-of-bounds read of the playlist.
	REQUIRE(NextTrackIndex(0, 4, -1) == 3);
	REQUIRE(NextTrackIndex(2, 4, -3) == 3);

	// A full turn is the same song, in both directions.
	REQUIRE(NextTrackIndex(1, 4, +4) == 1);
	REQUIRE(NextTrackIndex(1, 4, -4) == 1);

	// No songs, no index.
	REQUIRE(NextTrackIndex(0, 0, +1) == 0);
	REQUIRE(NextTrackIndex(0, 0, -1) == 0);
}

// ---------------------------------------------------------------------------
// The baked file
// ---------------------------------------------------------------------------

TEST_CASE("Every authored song in the playlist decodes through the engine", "[tetris][music]")
{
	// Resolved from the source tree rather than the working directory: the
	// game reads these paths relative to where it is run, and what needs
	// asserting here is that the files in the repo are playable by the
	// engine's decoder.
	const std::filesystem::path root = std::filesystem::absolute(__FILE__)
		.parent_path().parent_path().parent_path().parent_path();

	size_t authored = 0;
	for (const ThemeTrack& track : ThemePlaylist())
	{
		const std::filesystem::path file = root / track.Path;

		REQUIRE(std::filesystem::exists(file));
		++authored;

		INFO(track.Path);
		const Forge::Ref<Forge::AudioBuffer> buffer = Forge::AudioEngine::LoadAudioBuffer(file.string());
		REQUIRE(buffer);
		REQUIRE(buffer->GetChannels() == 2);
		REQUIRE(buffer->GetSampleRate() == 48000);
		REQUIRE(buffer->GetDuration() > 1.0);
	}

	REQUIRE(authored >= 3);
}

TEST_CASE("The baked Korobeiniki song decodes back as a full-length loop", "[tetris][music]")
{
	ScratchGuard scratch;

	const ThemeSong& song = GeneratedSong(0);
	const std::filesystem::path wav = kScratch / "korobeiniki.wav";
	REQUIRE(TetrisMusic::EnsureSongAsset(song, wav.string()));
	REQUIRE(std::filesystem::exists(wav));

	// Baking is idempotent: an existing file is left alone, so a second
	// call does not re-synthesise over the file it just wrote.
	REQUIRE(TetrisMusic::EnsureSongAsset(song, wav.string()));

	// Decoding is device independent, so this asserts the file is real audio
	// at the engine's rate with no sound card involved.
	const Forge::Ref<Forge::AudioBuffer> buffer = Forge::AudioEngine::LoadAudioBuffer(wav.string());
	REQUIRE(buffer);
	REQUIRE(buffer->GetChannels() == 2);
	REQUIRE(buffer->GetSampleRate() == 48000);
	REQUIRE(buffer->GetDuration() == Approx(SongLoopSeconds(song)).margin(0.05));
	REQUIRE(buffer->GetSamples() != nullptr);
}

TEST_CASE("Every generated song renders as a silent-seamed, in-range loop", "[tetris][music]")
{
	const uint32_t rate = 48000;

	for (const ThemeSong& song : GeneratedSongs())
	{
		INFO("song " << song.Id);

		// The form tiles the bars, the harmony covers every bar, and the
		// melody never points outside the form.
		uint32_t covered = 0;
		for (const ThemeSection& section : song.Sections)
			covered += section.BarCount;
		REQUIRE(covered == song.BarCount);
		REQUIRE(song.Chords.size() == song.BarCount);
		for (const ThemeMelodyNote& note : song.Melody)
			REQUIRE(note.Bar < song.BarCount);

		const std::vector<float> pcm = RenderSongPcm(song, rate);
		const size_t expectedFrames = SongLoopFrames(song, rate);
		REQUIRE(pcm.size() == expectedFrames * 2);

		// Nothing clips, and nothing is silent.
		REQUIRE(PeakAbs(pcm) <= 1.0f);
		REQUIRE(PeakAbs(pcm) > 0.30f);
		REQUIRE(RmsAbs(pcm, 0, pcm.size()) > 0.05f);

		// The loop seam: the last hundred milliseconds are silence, and the
		// wrap joins silence to the opening, not one waveform to another.
		const size_t tailFrames = rate / 10;
		REQUIRE(RmsAbs(pcm, (expectedFrames - tailFrames) * 2, pcm.size()) < 0.001f);
		REQUIRE(std::abs(pcm.back()) < 0.001f);
		REQUIRE(std::abs(pcm.front() - pcm.back()) < 0.01f);
	}
}
