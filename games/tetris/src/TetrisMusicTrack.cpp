#include "fgpch.h"
#include "TetrisMusicTrack.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace Tetris {

namespace {

// --- Tempo, form and grid ---

// 132 BPM: an eighth is 60/132/2 seconds. The three songs this borrows its
// grammar from run 125, 148 and 128 BPM; 132 sits between them - quick enough
// to move, slow enough to read the melody.
constexpr float kEighthSeconds = 0.25f * (120.0f / 132.0f);
// The folk transcription is 2/4, so a bar is four eighths - eight sixteenths,
// not sixteen. A sixteen-slot drum grid over a 2/4 bar is a half-bar out of
// phase with the tune, which is a groove that fights the music.
constexpr uint32_t kEighthsPerBar = 4;
[[maybe_unused]] constexpr uint32_t kSixteenthsPerBar = 8;
constexpr uint32_t kMelodyBars = 16;

// The song is twenty-eight bars: the sixteen-bar folk melody, an intro, a
// chorus, a breakdown and a reprise, so the loop has a shape instead of
// running flat for half a minute. Twenty-eight bars is about twenty-five
// seconds at 132 BPM - long enough to develop, short enough to bake at start.
constexpr uint32_t kBarCount = 28;
constexpr uint32_t kMelodyFirstBar = 4;
[[maybe_unused]] constexpr uint32_t kMelodySecondBar = 24;  // the reprise in the outro

// --- Mix ---

// The reference songs are sub-heavy (sub + bass are 39-56% of their energy),
// so the low end leads here too. These gains peak the mix near 0.72: loud
// enough to sit under the game, with headroom so nothing is clipped on write.
constexpr float kMasterGain = 0.62f;
constexpr float kKickGain = 0.52f;
constexpr float kSnareGain = 0.30f;
constexpr float kHatGain = 0.13f;
constexpr float kBassGain = 0.34f;
constexpr float kPadGain = 0.38f;
constexpr float kLeadGain = 0.92f;
constexpr float kArpGain = 0.26f;

// The octave the lead sings in, above its written pitch.
constexpr int16_t kLeadOctave = 12;

// The stretch at the very end of the song that nothing may sound into, so the
// loop wraps in silence. A quarter of a second is long enough for the longest
// release in the kit to finish.
constexpr float kLoopGuardSeconds = 0.25f;

// Side-chain: the pad and arp dip under every kick the way the reference mix
// breathes. The lead rides on top, undipped.
constexpr float kDuckDepth = 0.55f;
constexpr float kDuckAttack = 0.004f;
constexpr float kDuckRelease = 0.18f;

// --- Primitives ---

// Deterministic noise (xorshift32). Deterministic on purpose: a seed derived
// from bar and instrument keeps every render identical, so the loop seam, the
// peak and the tests all describe the same audio every time.
class Noise
{
public:
	explicit Noise(uint32_t seed) : m_State(seed == 0 ? 0x9E3779B9u : seed) {}

	float Next()
	{
		m_State ^= m_State << 13;
		m_State ^= m_State >> 17;
		m_State ^= m_State << 5;
		return static_cast<float>(m_State) * (1.0f / 2147483648.0f) - 1.0f;
	}

private:
	uint32_t m_State;
};

// A stereo bus the length of the whole song.
//
// Every bus keeps a guard of silence at the end. A sustained voice - a pad
// chord, an open hat - that is still sounding when the buffer ends is cut off
// mid-waveform, and that cut is a click, once per loop, forever. The guard
// makes the last stretch of the song unreachable to anything that rings, so the
// wrap happens in silence no matter what the arrangement does in its last bar.
class Bus
{
public:
	// `frames` is the loop's length as the song defines it (SongLoopFrames), not
	// a rounding of seconds done here: the frame the renderer stops at has to be
	// the frame the seam is measured against.
	Bus(uint32_t sampleRate, size_t frames, float tailGuardSeconds)
		: m_Frames(frames),
		m_Tail(static_cast<size_t>(tailGuardSeconds * static_cast<float>(sampleRate))),
		m_Data(m_Frames * 2, 0.0f)
	{
		if (m_Tail >= m_Frames / 4)
			m_Tail = m_Frames / 4;
	}

	size_t GetFrames() const { return m_Frames; }

	// The first frame the guard begins: nothing may sound from here on.
	size_t End() const { return m_Frames - m_Tail; }

	// How many frames a note starting at `startFrame` may occupy.
	size_t Clamp(size_t startFrame, size_t frames) const
	{
		if (startFrame >= End())
			return 0;

		return std::min(frames, End() - startFrame);
	}

	float* Data() { return m_Data.data(); }
	const float* Data() const { return m_Data.data(); }

	// Adds one sample, equal-power panned: -1 hard left, +1 hard right.
	void Add(size_t frame, float sample, float pan)
	{
		if (frame >= End())
			return;

		const float theta = (pan + 1.0f) * static_cast<float>(std::numbers::pi) * 0.25f;
		m_Data[frame * 2 + 0] += sample * std::cos(theta);
		m_Data[frame * 2 + 1] += sample * std::sin(theta);
	}

	void AddCentre(size_t frame, float sample)
	{
		if (frame >= End())
			return;

		m_Data[frame * 2 + 0] += sample;
		m_Data[frame * 2 + 1] += sample;
	}

private:
	size_t m_Frames;
	size_t m_Tail;
	std::vector<float> m_Data;
};

// Attack, decay-to-sustain, hold, release: the shape every note shares. The
// release reaches zero exactly at the end of the note, which keeps the
// waveform continuous - and a continuous waveform is what makes a loop
// seamless.
struct Envelope
{
	float AttackSeconds = 0.006f;
	float DecaySeconds = 0.060f;
	float Sustain = 0.75f;
	float ReleaseSeconds = 0.030f;

	float At(float t, float seconds) const
	{
		float level = 1.0f;
		if (t < AttackSeconds)
			level = t / AttackSeconds;
		else if (t < AttackSeconds + DecaySeconds)
			level = 1.0f + (Sustain - 1.0f) * ((t - AttackSeconds) / DecaySeconds);

		const float tRelease = seconds - t;
		if (tRelease < ReleaseSeconds)
			level *= tRelease / ReleaseSeconds;

		return level;
	}
};

// Harmonic weight sets. A saw falls off as 1/k; a square keeps only the odd
// harmonics, also at 1/k.
std::vector<float> SawWeights(int count)
{
	std::vector<float> weights;
	for (int k = 1; k <= count; ++k)
		weights.push_back(1.0f / static_cast<float>(k));
	return weights;
}

std::vector<float> SquareWeights(int count)
{
	std::vector<float> weights;
	for (int k = 1; k <= 2 * count - 1; k += 2)
		weights.push_back(1.0f / static_cast<float>(k));
	return weights;
}

std::vector<float> SineWeights()
{
	return { 1.0f };
}

float NoteFrequency(int16_t midi)
{
	return 440.0f * std::pow(2.0f, (static_cast<float>(midi) - 69.0f) / 12.0f);
}

// A band-limited additive oscillator.
//
// A naive saw or square has a hard edge, and the edge is a flat spectrum that
// folds back under the sample rate as audible noise - noise that moves with
// the pitch, so it sounds like a broken synth rather than a bright one.
// Summing the harmonics that fit under the Nyquist limit draws the same curve
// with nothing left to alias.
void AddOscillator(Bus& bus, uint32_t sampleRate, size_t startFrame, size_t frames,
	float frequency, float gain, float pan, const std::vector<float>& harmonicWeights, const Envelope& envelope)
{
	frames = bus.Clamp(startFrame, frames);
	if (frequency <= 0.0f || frames == 0 || gain <= 0.0f)
		return;

	const float seconds = static_cast<float>(frames) / static_cast<float>(sampleRate);
	const float twoPi = static_cast<float>(std::numbers::pi) * 2.0f;
	const float nyquist = 0.5f * static_cast<float>(sampleRate);

	std::vector<float> phase(harmonicWeights.size(), 0.0f);
	std::vector<float> step(harmonicWeights.size(), 0.0f);
	float weightSum = 0.0f;

	size_t harmonics = 0;
	for (; harmonics < harmonicWeights.size(); ++harmonics)
	{
		const float f = frequency * static_cast<float>(harmonics + 1);
		if (f >= nyquist)
			break;

		step[harmonics] = f * twoPi / static_cast<float>(sampleRate);
		weightSum += harmonicWeights[harmonics];
	}

	if (harmonics == 0 || weightSum <= 0.0f)
		return;

	for (size_t i = 0; i < frames; ++i)
	{
		float sample = 0.0f;
		for (size_t h = 0; h < harmonics; ++h)
		{
			sample += harmonicWeights[h] * std::sin(phase[h]);
			phase[h] += step[h];
			if (phase[h] > twoPi)
				phase[h] -= twoPi;
		}

		const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
		bus.Add(startFrame + i, (sample / weightSum) * envelope.At(t, seconds) * gain, pan);
	}
}

// The same oscillator, with a vibrato that fades in: long notes breathe and
// short ones stay clean.
void AddVibratoOscillator(Bus& bus, uint32_t sampleRate, size_t startFrame, size_t frames,
	float frequency, float gain, float pan, const std::vector<float>& harmonicWeights,
	const Envelope& envelope, float vibratoHz, float vibratoDepth, float vibratoFadeSeconds)
{
	frames = bus.Clamp(startFrame, frames);
	if (frequency <= 0.0f || frames == 0 || gain <= 0.0f)
		return;

	const float seconds = static_cast<float>(frames) / static_cast<float>(sampleRate);
	const float twoPi = static_cast<float>(std::numbers::pi) * 2.0f;
	const float nyquist = 0.5f * static_cast<float>(sampleRate);

	std::vector<float> phase(harmonicWeights.size(), 0.0f);
	float weightSum = 0.0f;
	size_t harmonics = 0;
	for (; harmonics < harmonicWeights.size(); ++harmonics)
	{
		if (frequency * static_cast<float>(harmonics + 1) >= nyquist)
			break;
		weightSum += harmonicWeights[harmonics];
	}

	if (harmonics == 0 || weightSum <= 0.0f)
		return;

	for (size_t i = 0; i < frames; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(sampleRate);

		const float fade = vibratoFadeSeconds > 0.0f
			? std::min(t / vibratoFadeSeconds, 1.0f) : 1.0f;
		const float vibrato = 1.0f + vibratoDepth * std::sin(twoPi * vibratoHz * t) * fade;
		const float f = frequency * vibrato;

		float sample = 0.0f;
		for (size_t h = 0; h < harmonics; ++h)
		{
			sample += harmonicWeights[h] * std::sin(phase[h]);
			phase[h] += f * static_cast<float>(h + 1) * twoPi / static_cast<float>(sampleRate);
			if (phase[h] > twoPi)
				phase[h] -= twoPi;
		}

		bus.Add(startFrame + i, (sample / weightSum) * envelope.At(t, seconds) * gain, pan);
	}
}

// Percussion: filtered noise. The filters are the instrument - the same white
// noise through a closed filter is a snare, through an open one a hat.
void AddNoise(Bus& bus, size_t startFrame, size_t frames, Noise& noise, float gain, float pan,
	float lowpassHz, float highpassHz, uint32_t sampleRate, const Envelope& envelope)
{
	frames = bus.Clamp(startFrame, frames);
	if (frames == 0 || gain <= 0.0f)
		return;

	const float dt = 1.0f / static_cast<float>(sampleRate);
	const float nyquist = 0.5f * static_cast<float>(sampleRate);

	// One-pole coefficients from the corner frequencies, each held below
	// Nyquist so a corner above it cannot make the filter unstable.
	const float lowCorner = std::min(lowpassHz, nyquist * 0.95f);
	const float highCorner = std::min(highpassHz, nyquist * 0.95f);
	const float aLow = 1.0f - std::exp(-2.0f * static_cast<float>(std::numbers::pi) * lowCorner * dt);
	const float aHigh = 1.0f - std::exp(-2.0f * static_cast<float>(std::numbers::pi) * highCorner * dt);

	const float seconds = static_cast<float>(frames) / static_cast<float>(sampleRate);

	float low = 0.0f;
	float high = 0.0f;
	for (size_t i = 0; i < frames; ++i)
	{
		const float white = noise.Next();

		low += aLow * (white - low);         // one-pole lowpass
		high = aHigh * (low - high) + high;  // one-pole highpass of the low

		const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
		bus.Add(startFrame + i, high * envelope.At(t, seconds) * gain, pan);
	}
}

// The kick: a sine whose pitch drops from a click down to a thud. The pitch
// envelope is what makes a kick read as a kick; without it the same tone is
// just a bass note.
void AddKick(Bus& bus, size_t startFrame, uint32_t sampleRate, float gain)
{
	const float startHz = 120.0f;
	const float endHz = 45.0f;
	const float pitchDropSeconds = 0.040f;
	const float ampSeconds = 0.055f;
	const float seconds = 0.20f;
	size_t frames = bus.Clamp(startFrame, static_cast<size_t>(seconds * static_cast<float>(sampleRate)));

	const float twoPi = static_cast<float>(std::numbers::pi) * 2.0f;
	float phase = 0.0f;

	for (size_t i = 0; i < frames; ++i)
	{
		const float t = static_cast<float>(i) / static_cast<float>(sampleRate);
		const float pitch = endHz + (startHz - endHz) * std::exp(-t / pitchDropSeconds);

		phase += pitch * twoPi / static_cast<float>(sampleRate);
		if (phase > twoPi)
			phase -= twoPi;

		// The amplitude decays faster than the pitch, so the tail is a tone
		// rather than a hum, and it reaches zero at the end of the hit.
		float amp = std::exp(-t / ampSeconds);
		const float tRelease = seconds - t;
		if (tRelease < 0.035f)
			amp *= tRelease / 0.035f;

		bus.AddCentre(startFrame + i, std::sin(phase) * amp * gain);
	}
}

// The snare: filtered noise for the rattle plus two tuned body tones, because
// a snare is a drum with a pitch and noise alone is just a hiss.
void AddSnare(Bus& bus, size_t startFrame, uint32_t sampleRate, float gain, uint32_t seed)
{
	const float seconds = 0.16f;
	const size_t frames = bus.Clamp(startFrame, static_cast<size_t>(seconds * static_cast<float>(sampleRate)));

	Noise noise(seed);
	AddNoise(bus, startFrame, frames, noise, gain * 0.9f, 0.0f, 6000.0f, 300.0f, sampleRate,
		Envelope{ 0.001f, 0.030f, 0.25f, 0.060f });

	AddOscillator(bus, sampleRate, startFrame, frames, 185.0f, gain * 0.4f, 0.0f, SineWeights(),
		Envelope{ 0.001f, 0.020f, 0.2f, 0.050f });
	AddOscillator(bus, sampleRate, startFrame, frames, 330.0f, gain * 0.25f, 0.0f, SineWeights(),
		Envelope{ 0.001f, 0.018f, 0.2f, 0.045f });
}

// The closed hat is short; the open hat is the same noise with a longer decay,
// which is the only real difference between them.
void AddHat(Bus& bus, size_t startFrame, uint32_t sampleRate, float gain, bool open, uint32_t seed)
{
	const float seconds = open ? 0.22f : 0.045f;
	const size_t frames = bus.Clamp(startFrame, static_cast<size_t>(seconds * static_cast<float>(sampleRate)));

	Noise noise(seed);
	AddNoise(bus, startFrame, frames, noise, gain, open ? 0.35f : -0.35f, 18000.0f, 5200.0f, sampleRate,
		Envelope{ 0.001f, 0.010f, 0.2f, open ? 0.14f : 0.020f });
}

// --- Pitched voices ---

// The bass: a short saw plus the fundamental an octave down for the sub weight
// the reference mix is built on. Driving eighths, as in all three songs.
void AddBassNote(Bus& bus, uint32_t sampleRate, size_t startFrame, float units, int16_t midi, float gainScale)
{
	const float seconds = units * kEighthSeconds;
	const size_t frames = bus.Clamp(startFrame, static_cast<size_t>(seconds * static_cast<float>(sampleRate)));
	const float frequency = NoteFrequency(midi);

	const Envelope envelope{ 0.004f, 0.050f, 0.60f, 0.040f };
	const float gain = kBassGain * gainScale;

	AddOscillator(bus, sampleRate, startFrame, frames, frequency, gain, 0.0f, SawWeights(5), envelope);
	AddOscillator(bus, sampleRate, startFrame, frames, frequency * 0.5f, gain * 0.55f, 0.0f,
		SineWeights(), envelope);
}

// The pad: chord tones each doubled a few cents apart and spread across the
// field. Detuning is what makes a chord shimmer instead of sitting still, and
// it costs nothing but the voices.
void AddPadChord(Bus& bus, uint32_t sampleRate, size_t startFrame, float units,
	int16_t root, int16_t third, float gainScale)
{
	const float seconds = units * kEighthSeconds;
	const size_t frames = bus.Clamp(startFrame, static_cast<size_t>(seconds * static_cast<float>(sampleRate)));

	const float tones[2] = { NoteFrequency(static_cast<int16_t>(root + 12)), NoteFrequency(static_cast<int16_t>(third + 12)) };
	const float pans[2] = { -0.5f, 0.5f };
	const Envelope envelope{ 0.120f, 0.200f, 0.70f, 0.300f };

	for (int v = 0; v < 2; ++v)
	{
		const float detuneUp = 1.0f + 0.004f;
		const float detuneDown = 1.0f - 0.004f;

		AddOscillator(bus, sampleRate, startFrame, frames, tones[v] * detuneUp,
			kPadGain * 0.5f * gainScale, pans[v], SawWeights(5), envelope);
		AddOscillator(bus, sampleRate, startFrame, frames, tones[v] * detuneDown,
			kPadGain * 0.45f * gainScale, -pans[v], SawWeights(5), envelope);
	}
}

// The lead sings an octave above the melody's written pitch. The folk tune
// sits at D4..D5, which is bass-register for a tune line: at that pitch its
// fundamental lands in the same band as the bass and the two fight for the
// same space. An octave up puts the line in the mids, where a chiptune lead
// belongs and where the reference songs carry theirs.
void AddLeadNote(Bus& bus, uint32_t sampleRate, size_t startFrame, float units, int16_t midi, float gainScale)
{
	const float seconds = units * kEighthSeconds;
	const size_t frames = bus.Clamp(startFrame, static_cast<size_t>(seconds * static_cast<float>(sampleRate)));
	const float frequency = NoteFrequency(midi);

	const Envelope envelope{ 0.008f, 0.080f, 0.78f, 0.040f };

	// Two copies: one centred, one a hair to the left, so the lead sits
	// forward without being dead centre.
	AddVibratoOscillator(bus, sampleRate, startFrame, frames, frequency, kLeadGain * gainScale, 0.0f,
		SquareWeights(16), envelope, 5.0f, 0.006f, 0.12f);
	AddVibratoOscillator(bus, sampleRate, startFrame, frames, frequency * 1.003f, kLeadGain * 0.4f * gainScale, -0.2f,
		SquareWeights(16), envelope, 5.0f, 0.006f, 0.12f);
}

// The arp: a short, soft blip doubling the bass motion in the chorus - the
// shimmer the reference tracks carry over their drums.
void AddArpNote(Bus& bus, uint32_t sampleRate, size_t startFrame, int16_t midi, float gainScale)
{
	const float seconds = 0.12f;
	const size_t frames = bus.Clamp(startFrame, static_cast<size_t>(seconds * static_cast<float>(sampleRate)));
	const float frequency = NoteFrequency(midi);

	AddOscillator(bus, sampleRate, startFrame, frames, frequency, kArpGain * gainScale, 0.3f, SawWeights(4),
		Envelope{ 0.003f, 0.040f, 0.15f, 0.050f });
}

// --- The arrangement ---

// A chord per bar: minor on D and G, major on A - i, iv, V under the folk tune.
struct Chord
{
	int16_t Root;
	int16_t Third;
};

Chord ChordForRoot(int16_t root)
{
	if (root == 50) return { 50, 53 };  // D minor
	if (root == 43) return { 43, 46 };  // G minor
	return { 45, 52 };                 // A major
}

}  // namespace

const std::vector<MusicNote>& KorobeinikiMelody()
{
	// Sixteen bars of four eighth units each, transcribed from the folk
	// version in Jack Campin's Nine-Note Tunebook:
	//
	//     M:2/4  L:1/8  K:DMin
	//     A2 EF | G2 TFE | D2 DF | A2 GF | E3 F | G2 A2 | F2 D2 | D2 z2 ||
	//     G2-GB | d2  cB | A3 F | A2 GF | E2 EF | G2 A2 | F2 D2 | D2 z2 |]
	//
	// The bar-2 triplet is played straight: at chiptune speeds a swing of two
	// thirds of an eighth is inaudible, and writing it as three straight notes
	// keeps the bar exactly four units, which is what the loop maths depends on.
	static const std::vector<MusicNote> melody = {
		{ 69, 2 }, { 64, 1 }, { 65, 1 },   // A4  E4 F4
		{ 67, 2 }, { 65, 1 }, { 64, 1 },   // G4  F4 E4
		{ 62, 2 }, { 62, 1 }, { 65, 1 },   // D4  D4 F4
		{ 69, 2 }, { 67, 1 }, { 65, 1 },   // A4  G4 F4
		{ 64, 3 }, { 65, 1 },              // E4      F4
		{ 67, 2 }, { 69, 2 },              // G4  A4
		{ 65, 2 }, { 62, 2 },              // F4  D4
		{ 62, 2 }, {  0, 2 },              // D4  rest

		{ 67, 3 }, { 71, 1 },              // G4      B4
		{ 74, 2 }, { 72, 1 }, { 71, 1 },   // D5  C5 B4
		{ 69, 3 }, { 65, 1 },              // A4      F4
		{ 69, 2 }, { 67, 1 }, { 65, 1 },   // A4  G4 F4
		{ 64, 2 }, { 64, 1 }, { 65, 1 },   // E4  E4 F4
		{ 67, 2 }, { 69, 2 },              // G4  A4
		{ 65, 2 }, { 62, 2 },              // F4  D4
		{ 62, 2 }, {  0, 2 },              // D4  rest
	};

	return melody;
}

const std::vector<int16_t>& ThemeBassRoots()
{
	// One root per bar of the melody: mostly i, a V in bars 5 and 11, iv in
	// bars 9 and 10. The repeats are where the folk tune leans on a pedal.
	static const std::vector<int16_t> roots = {
		50, 50, 50, 50, 45, 50, 50, 50,
		43, 43, 45, 50, 45, 50, 50, 50
	};
	return roots;
}

float ThemeEighthSeconds() { return kEighthSeconds; }
float ThemeBarSeconds() { return kEighthSeconds * static_cast<float>(kEighthsPerBar); }
uint32_t ThemeBarCount() { return kBarCount; }
float ThemeLoopSeconds() { return static_cast<float>(kBarCount) * ThemeBarSeconds(); }

const DrumPattern& ThemeKickPattern()
{
	// On every beat, accented on the offbeats: the four-to-the-floor-adjacent
	// pattern all three reference songs share.
	static const DrumPattern pattern = {
		1.0f, 0.0f, 0.55f, 0.0f, 1.0f, 0.0f, 0.55f, 0.35f
	};
	return pattern;
}

const DrumPattern& ThemeSnarePattern()
{
	// Backbeat on 2 and 4, with a whisper of ghost note before each.
	static const DrumPattern pattern = {
		0.0f, 0.0f, 0.0f, 0.10f, 1.0f, 0.0f, 0.0f, 0.15f
	};
	return pattern;
}

const DrumPattern& ThemeHatPattern()
{
	// Eighth-note hats with the offbeats accented, matching the reference
	// mixes' even/odd energy ratio under 1.
	static const DrumPattern pattern = {
		0.6f, 0.0f, 0.45f, 0.0f, 0.6f, 0.0f, 0.45f, 0.5f
	};
	return pattern;
}

const std::vector<ThemeSection>& ThemeArrangement()
{
	// A form with somewhere to go: intro (groove enters), verse, chorus (the
	// peak), bridge (drums drop, pads hold), and an outro that reprises the
	// tune and thins into the loop point. The energy profile matches the
	// reference tracks' rising, dipping arcs.
	static const std::vector<ThemeSection> sections = {
		{ "Intro", 0, 4 },
		{ "Verse", 4, 8 },
		{ "Chorus", 12, 8 },
		{ "Bridge", 20, 4 },
		{ "Outro", 24, 4 },
	};
	return sections;
}

float ThemePitchForLevel(int32_t level)
{
	if (level <= 1)
		return 1.0f;

	// 4.5% per level, capped. The cap matters: pitch is a sample-rate
	// multiplier, so an uncapped ramp turns the tune into a chipmunk and the
	// lead out of the range the voices were written for.
	return std::min(1.0f + 0.045f * static_cast<float>(level - 1), 1.45f);
}

MusicIntent ThemeIntentForState(TetrisGameState::State state, bool muted)
{
	if (muted)
		return MusicIntent::Stop;

	// The menu is silent on purpose: the tune is the sound of playing, so it
	// starts with the game (R) and not with the window.
	return state == TetrisGameState::State::Playing ? MusicIntent::Start : MusicIntent::Stop;
}

const std::vector<ThemeTrack>& ThemePlaylist()
{
	// The authored tracks, in the order the game plays them. The generated
	// arrangements are not in the playlist: the reconstructions measured
	// worse than the recordings they were made from, so the originals play
	// and the generator stays as the engine's own Korobeiniki theme and as
	// the subject of its tests.
	static const std::vector<ThemeTrack> playlist = {
		{ "Block Drop", "resources/audio/tetris_block_drop.mp3", nullptr },
		{ "Block Stacker", "resources/audio/tetris_block_stacker.mp3", nullptr },
		{ "Falling Blocks", "resources/audio/tetris_falling_blocks.mp3", nullptr },
	};

	return playlist;
}

int NextTrackIndex(int index, int count, int delta)
{
	if (count <= 0)
		return 0;

	// C++'s % keeps the sign of the dividend, so skipping backwards from the
	// first song would produce a negative index. Adding a full turn before the
	// second modulo is what makes -1 from 0 land on the last song.
	return ((index + delta) % count + count) % count;
}

// The three songs reconstructed from the authored tracks in
// resources/audio, by analyse.py and distill.py: tempo, key, chord loop, drum
// pattern and one phrase of melody each, all recovered from the recordings and
// distilled to a sixteen-bar loop. What is faithful is the groove and the
// harmony; the melody is what the analysis could hear, not what was written.
const std::vector<ThemeSong>& ReconstructedSongs()
{
	static const std::vector<ThemeSong> songs = [] {
		std::vector<ThemeSong> all;
		{
			ThemeSong song;
	// Block Drop: 148.5 BPM, analysed from the authored track.
	song.Id = "block_drop";
	song.Mix = { 0.80f, 1.00f, 2.00f, 0.70f, 1.15f, 1.80f, 1.00f };
	song.Title = "Block Drop";
	song.EighthSeconds = 0.202020f;
	song.EighthsPerBar = 8;
	song.BarCount = 16;
	song.Sections = { { "Verse", 0, 8 }, { "Chorus", 8, 8 } };

	song.Chords = {
		{ 44, 47 }, { 35, 38 }, { 44, 47 }, { 35, 38 },
		{ 44, 47 }, { 35, 38 }, { 44, 47 }, { 35, 38 },
		{ 44, 47 }, { 35, 38 }, { 44, 47 }, { 35, 38 },
		{ 44, 47 }, { 35, 38 }, { 44, 47 }, { 35, 38 },
	};

	song.Melody = {
		{ 0, 80, 0, 1 },
		{ 0, 73, 1, 1 },
		{ 0, 70, 2, 1 },
		{ 0, 68, 3, 1 },
		{ 0, 70, 4, 1 },
		{ 0, 68, 5, 1 },
		{ 0, 70, 6, 1 },
		{ 0, 68, 7, 1 },
		{ 1, 80, 0, 1 },
		{ 1, 73, 1, 1 },
		{ 1, 70, 2, 1 },
		{ 1, 68, 3, 1 },
		{ 1, 70, 4, 1 },
		{ 1, 68, 5, 1 },
		{ 1, 70, 6, 1 },
		{ 1, 68, 7, 1 },
		{ 2, 80, 0, 1 },
		{ 2, 73, 1, 1 },
		{ 2, 70, 2, 1 },
		{ 2, 68, 3, 1 },
		{ 2, 70, 4, 1 },
		{ 2, 68, 5, 1 },
		{ 2, 70, 6, 1 },
		{ 2, 68, 7, 1 },
		{ 3, 80, 0, 1 },
		{ 3, 73, 1, 1 },
		{ 3, 70, 2, 1 },
		{ 3, 68, 3, 1 },
		{ 3, 70, 4, 1 },
		{ 3, 68, 5, 1 },
		{ 3, 70, 6, 1 },
		{ 3, 68, 7, 1 },
		{ 8, 80, 0, 1 },
		{ 8, 73, 1, 1 },
		{ 8, 70, 2, 1 },
		{ 8, 68, 3, 1 },
		{ 8, 70, 4, 1 },
		{ 8, 68, 5, 1 },
		{ 8, 70, 6, 1 },
		{ 8, 68, 7, 1 },
		{ 9, 80, 0, 1 },
		{ 9, 73, 1, 1 },
		{ 9, 70, 2, 1 },
		{ 9, 68, 3, 1 },
		{ 9, 70, 4, 1 },
		{ 9, 68, 5, 1 },
		{ 9, 70, 6, 1 },
		{ 9, 68, 7, 1 },
		{ 10, 80, 0, 1 },
		{ 10, 73, 1, 1 },
		{ 10, 70, 2, 1 },
		{ 10, 68, 3, 1 },
		{ 10, 70, 4, 1 },
		{ 10, 68, 5, 1 },
		{ 10, 70, 6, 1 },
		{ 10, 68, 7, 1 },
		{ 11, 80, 0, 1 },
		{ 11, 73, 1, 1 },
		{ 11, 70, 2, 1 },
		{ 11, 68, 3, 1 },
		{ 11, 70, 4, 1 },
		{ 11, 68, 5, 1 },
		{ 11, 70, 6, 1 },
		{ 11, 68, 7, 1 },
	};

	song.Kick = { 0.84f, 0.88f, 1.00f, 0.60f, 0.64f, 0.66f, 0.65f, 0.61f, 0.98f, 0.76f, 0.89f, 0.87f, 0.71f, 0.89f, 0.65f, 0.87f };
	song.Snare = { 0.91f, 0.91f, 0.81f, 0.92f, 0.80f, 0.96f, 0.87f, 0.00f, 0.79f, 0.86f, 0.87f, 0.00f, 0.81f, 1.00f, 0.92f, 0.90f };
	song.Hat = { 0.00f, 0.85f, 0.84f, 0.73f, 0.81f, 1.00f, 0.81f, 0.00f, 0.00f, 0.70f, 0.77f, 0.00f, 0.00f, 0.86f, 0.82f, 0.79f };
			all.push_back(std::move(song));
		}
		{
			ThemeSong song;
	// Block Stacker: 125.0 BPM, analysed from the authored track.
	song.Id = "block_stacker";
	song.Mix = { 1.05f, 1.00f, 2.00f, 0.45f, 1.30f, 2.10f, 1.00f };
	song.Title = "Block Stacker";
	song.EighthSeconds = 0.240000f;
	song.EighthsPerBar = 8;
	song.BarCount = 16;
	song.Sections = { { "Verse", 0, 8 }, { "Chorus", 8, 8 } };

	song.Chords = {
		{ 44, 47 }, { 35, 38 }, { 44, 47 }, { 35, 38 },
		{ 44, 47 }, { 35, 38 }, { 44, 47 }, { 35, 38 },
		{ 44, 47 }, { 35, 38 }, { 44, 47 }, { 35, 38 },
		{ 44, 47 }, { 35, 38 }, { 44, 47 }, { 35, 38 },
	};

	song.Melody = {
		{ 0, 70, 0, 1 },
		{ 0, 68, 1, 1 },
		{ 0, 61, 2, 1 },
		{ 0, 65, 3, 1 },
		{ 0, 68, 4, 1 },
		{ 0, 65, 5, 1 },
		{ 0, 68, 6, 1 },
		{ 0, 70, 7, 1 },
		{ 1, 70, 0, 1 },
		{ 1, 68, 1, 1 },
		{ 1, 61, 2, 1 },
		{ 1, 65, 3, 1 },
		{ 1, 68, 4, 1 },
		{ 1, 65, 5, 1 },
		{ 1, 68, 6, 1 },
		{ 1, 70, 7, 1 },
		{ 2, 70, 0, 1 },
		{ 2, 68, 1, 1 },
		{ 2, 61, 2, 1 },
		{ 2, 65, 3, 1 },
		{ 2, 68, 4, 1 },
		{ 2, 65, 5, 1 },
		{ 2, 68, 6, 1 },
		{ 2, 70, 7, 1 },
		{ 3, 70, 0, 1 },
		{ 3, 68, 1, 1 },
		{ 3, 61, 2, 1 },
		{ 3, 65, 3, 1 },
		{ 3, 68, 4, 1 },
		{ 3, 65, 5, 1 },
		{ 3, 68, 6, 1 },
		{ 3, 70, 7, 1 },
		{ 8, 70, 0, 1 },
		{ 8, 68, 1, 1 },
		{ 8, 61, 2, 1 },
		{ 8, 65, 3, 1 },
		{ 8, 68, 4, 1 },
		{ 8, 65, 5, 1 },
		{ 8, 68, 6, 1 },
		{ 8, 70, 7, 1 },
		{ 9, 70, 0, 1 },
		{ 9, 68, 1, 1 },
		{ 9, 61, 2, 1 },
		{ 9, 65, 3, 1 },
		{ 9, 68, 4, 1 },
		{ 9, 65, 5, 1 },
		{ 9, 68, 6, 1 },
		{ 9, 70, 7, 1 },
		{ 10, 70, 0, 1 },
		{ 10, 68, 1, 1 },
		{ 10, 61, 2, 1 },
		{ 10, 65, 3, 1 },
		{ 10, 68, 4, 1 },
		{ 10, 65, 5, 1 },
		{ 10, 68, 6, 1 },
		{ 10, 70, 7, 1 },
		{ 11, 70, 0, 1 },
		{ 11, 68, 1, 1 },
		{ 11, 61, 2, 1 },
		{ 11, 65, 3, 1 },
		{ 11, 68, 4, 1 },
		{ 11, 65, 5, 1 },
		{ 11, 68, 6, 1 },
		{ 11, 70, 7, 1 },
	};

	song.Kick = { 0.00f, 1.00f, 0.00f, 0.00f, 0.69f, 0.83f, 0.00f, 0.81f, 0.00f, 0.82f, 0.64f, 0.00f, 0.68f, 0.78f, 0.00f, 0.67f };
	song.Snare = { 0.00f, 0.98f, 0.74f, 0.81f, 0.00f, 0.98f, 0.00f, 0.77f, 0.00f, 0.73f, 0.72f, 0.00f, 0.00f, 1.00f, 0.00f, 0.00f };
	song.Hat = { 0.00f, 0.99f, 0.00f, 1.00f, 0.00f, 0.79f, 0.00f, 0.84f, 0.00f, 0.79f, 0.00f, 0.89f, 0.00f, 0.94f, 0.00f, 0.00f };
			all.push_back(std::move(song));
		}
		{
			ThemeSong song;
	// Falling Blocks: 128.0 BPM, analysed from the authored track.
	song.Id = "falling_blocks";
	song.Mix = { 0.95f, 1.00f, 2.10f, 0.85f, 1.20f, 1.55f, 1.00f };
	song.Title = "Falling Blocks";
	song.EighthSeconds = 0.234375f;
	song.EighthsPerBar = 8;
	song.BarCount = 16;
	song.Sections = { { "Verse", 0, 8 }, { "Chorus", 8, 8 } };

	song.Chords = {
		{ 42, 45 }, { 45, 48 }, { 40, 43 }, { 42, 45 },
		{ 42, 45 }, { 45, 48 }, { 40, 43 }, { 42, 45 },
		{ 42, 45 }, { 45, 48 }, { 40, 43 }, { 42, 45 },
		{ 42, 45 }, { 45, 48 }, { 40, 43 }, { 42, 45 },
	};

	song.Melody = {
		{ 0, 68, 0, 1 },
		{ 0, 71, 1, 2 },
		{ 0, 75, 3, 1 },
		{ 0, 78, 4, 2 },
		{ 0, 75, 6, 1 },
		{ 0, 71, 7, 1 },
		{ 1, 68, 0, 1 },
		{ 1, 71, 1, 2 },
		{ 1, 75, 3, 1 },
		{ 1, 78, 4, 2 },
		{ 1, 75, 6, 1 },
		{ 1, 71, 7, 1 },
		{ 2, 68, 0, 1 },
		{ 2, 71, 1, 2 },
		{ 2, 75, 3, 1 },
		{ 2, 78, 4, 2 },
		{ 2, 75, 6, 1 },
		{ 2, 71, 7, 1 },
		{ 3, 68, 0, 1 },
		{ 3, 71, 1, 2 },
		{ 3, 75, 3, 1 },
		{ 3, 78, 4, 2 },
		{ 3, 75, 6, 1 },
		{ 3, 71, 7, 1 },
		{ 8, 68, 0, 1 },
		{ 8, 71, 1, 2 },
		{ 8, 75, 3, 1 },
		{ 8, 78, 4, 2 },
		{ 8, 75, 6, 1 },
		{ 8, 71, 7, 1 },
		{ 9, 68, 0, 1 },
		{ 9, 71, 1, 2 },
		{ 9, 75, 3, 1 },
		{ 9, 78, 4, 2 },
		{ 9, 75, 6, 1 },
		{ 9, 71, 7, 1 },
		{ 10, 68, 0, 1 },
		{ 10, 71, 1, 2 },
		{ 10, 75, 3, 1 },
		{ 10, 78, 4, 2 },
		{ 10, 75, 6, 1 },
		{ 10, 71, 7, 1 },
		{ 11, 68, 0, 1 },
		{ 11, 71, 1, 2 },
		{ 11, 75, 3, 1 },
		{ 11, 78, 4, 2 },
		{ 11, 75, 6, 1 },
		{ 11, 71, 7, 1 },
	};

	song.Kick = { 0.85f, 0.73f, 0.68f, 0.00f, 0.72f, 0.00f, 0.68f, 0.00f, 1.00f, 0.00f, 0.81f, 0.00f, 0.72f, 0.00f, 0.57f, 0.71f };
	song.Snare = { 0.00f, 0.84f, 0.59f, 0.00f, 0.71f, 0.00f, 0.67f, 0.00f, 0.00f, 0.00f, 0.66f, 1.00f, 0.86f, 0.00f, 0.66f, 0.77f };
	song.Hat = { 0.00f, 0.00f, 0.65f, 0.00f, 0.76f, 0.00f, 0.68f, 0.00f, 0.00f, 0.00f, 0.69f, 1.00f, 0.79f, 0.00f, 0.68f, 0.00f };
			all.push_back(std::move(song));
		}
		return all;
	}();
	return songs;
}

float SongBarSeconds(const ThemeSong& song)
{
	return song.EighthSeconds * static_cast<float>(song.EighthsPerBar);
}

float SongLoopSeconds(const ThemeSong& song)
{
	return static_cast<float>(song.BarCount) * SongBarSeconds(song);
}

size_t SongLoopFrames(const ThemeSong& song, uint32_t sampleRate)
{
	if (sampleRate == 0)
		return 0;

	return static_cast<size_t>(SongLoopSeconds(song) * static_cast<float>(sampleRate) + 0.5f);
}

// The Korobeiniki theme as a song: its melody laid out bar by bar, a chord per
// bar from the folk progression, the standard 2/4 drums, and the form. Built
// once from the tables above, so there is one description of the theme and the
// renderer treats it exactly like a reconstructed one.
static ThemeSong BuildKorobeinikiSong()
{
	ThemeSong song;
	song.Id = "korobeiniki";
	song.Title = "Korobeiniki";
	song.EighthSeconds = kEighthSeconds;
	song.EighthsPerBar = kEighthsPerBar;
	song.BarCount = kBarCount;

	song.Sections = ThemeArrangement();
	song.Kick = ThemeKickPattern();
	song.Snare = ThemeSnarePattern();
	song.Hat = ThemeHatPattern();

	// A chord per bar, cycling the sixteen-bar progression; the arrangement
	// already plays the melody twice, so the harmony follows it round.
	const std::vector<int16_t>& roots = ThemeBassRoots();
	for (uint32_t bar = 0; bar < kBarCount; ++bar)
	{
		const int16_t root = roots[bar % roots.size()];
		const Chord chord = ChordForRoot(root);
		song.Chords.push_back({ root, chord.Third });
	}

	// The melody, laid into the bars that carry it. The theme sings the folk tune
	// in the verse and reprises it in the outro, so the notes are emitted for
	// both passes, each with the bar it lands in.
	const std::vector<MusicNote>& melody = KorobeinikiMelody();

	float units = 0.0f;
	uint32_t bar = 0;
	for (const MusicNote& note : melody)
	{
		if (note.Midi > 0)
		{
			const float offsetInBar = units - static_cast<float>(bar) * static_cast<float>(kEighthsPerBar);
			const ThemeMelodyNote placed{
				0,
				static_cast<int16_t>(note.Midi + kLeadOctave),
				static_cast<uint8_t>(offsetInBar * 2.0f),
				static_cast<uint8_t>(note.Units * 2.0f) };

			for (uint32_t pass = 0; pass < 2; ++pass)
			{
				const uint32_t targetBar = kMelodyFirstBar + bar + pass * kMelodyBars;
				if (targetBar < kBarCount)
					song.Melody.push_back({ targetBar, placed.Midi,
						placed.OffsetEighths, placed.DurationEighths });
			}
		}

		units += note.Units;
		if (units >= static_cast<float>(bar + 1) * static_cast<float>(kEighthsPerBar))
			++bar;
	}

	return song;
}

const std::vector<ThemeSong>& GeneratedSongs()
{
	static const std::vector<ThemeSong> songs = [] {
		std::vector<ThemeSong> all;
		all.push_back(BuildKorobeinikiSong());
		for (const ThemeSong& song : ReconstructedSongs())
			all.push_back(song);
		return all;
	}();
	return songs;
}

const ThemeSong& GeneratedSong(size_t index)
{
	const std::vector<ThemeSong>& songs = GeneratedSongs();
	return songs[std::min(index, songs.size() - 1)];
}

std::vector<float> RenderThemePcm(uint32_t sampleRate)
{
	return RenderSongPcm(GeneratedSong(0), sampleRate);
}

std::vector<float> RenderSongPcm(const ThemeSong& song, uint32_t sampleRate)
{
	if (sampleRate == 0)
		return {};

	const float eighthSeconds = song.EighthSeconds;
	const uint32_t eighthsPerBar = song.EighthsPerBar;
	const uint32_t sixteenthsPerBar = eighthsPerBar * 2;

	Bus music(sampleRate, SongLoopFrames(song, sampleRate), kLoopGuardSeconds);
	Bus drums(sampleRate, SongLoopFrames(song, sampleRate), kLoopGuardSeconds);

	const size_t frames = music.GetFrames();
	const float framesPerBar = SongBarSeconds(song) * static_cast<float>(sampleRate);
	const float framesPerUnit = static_cast<float>(sampleRate) * eighthSeconds;
	const float framesPerSixteenth = framesPerBar / static_cast<float>(sixteenthsPerBar);

	const auto sectionForBar = [&song](uint32_t bar) -> const ThemeSection&
	{
		for (const ThemeSection& section : song.Sections)
		{
			if (bar >= section.FirstBar && bar < section.FirstBar + section.BarCount)
				return section;
		}
		return song.Sections.back();
	};

	const auto named = [&sectionForBar](uint32_t bar, const char* name)
	{
		return std::string(sectionForBar(bar).Name) == name;
	};

	// The lead's notes grouped by the bar they fall in, so the bar loop can
	// place them without rescanning the melody per bar.
	std::vector<std::vector<const ThemeMelodyNote*>> melodyInBar(song.BarCount);
	for (const ThemeMelodyNote& note : song.Melody)
	{
		if (note.Bar < song.BarCount)
			melodyInBar[note.Bar].push_back(&note);
	}

	// The kick envelope, for the side-chain: where the kick lands and how fast
	// it recovers.
	std::vector<float> kickEnvelope(frames, 0.0f);
	const float kickDecaySeconds = 0.05f;

	for (uint32_t bar = 0; bar < song.BarCount; ++bar)
	{
		const size_t barStart = static_cast<size_t>(static_cast<float>(bar) * framesPerBar);

		// What part of the form this bar is, and what that means for weight.
		const bool intro = named(bar, "Intro");
		const bool bridge = named(bar, "Bridge");
		const bool lastBar = bar + 1 == song.BarCount;

		// --- Drums ---
		//
		// The song's own pattern, gated by the form: the intro ticks hats and
		// a ghost kick so the pulse arrives before the weight does, the bridge
		// drops the weight and keeps the hats, and the last bar lets the loop
		// point breathe.
		{
			const float kickScale = intro ? 0.62f : 1.0f;
			const float hatScale = (intro || bridge) ? 0.7f : 1.0f;
			const bool weight = !bridge && !lastBar;

			for (uint32_t step = 0; step < sixteenthsPerBar; ++step)
			{
				if (step >= song.Kick.size())
					break;

				const size_t frame = barStart + static_cast<size_t>(static_cast<float>(step) * framesPerSixteenth);

				const float kick = song.Kick[step];
				if (kick > 0.0f && weight)
				{
					AddKick(drums, frame, sampleRate, kKickGain * kick * kickScale * song.Mix.Kick);

					// The kick's envelope is remembered for the side-chain, so
					// the duck follows the beat rather than a parallel timer
					// that would drift away from it.
					const size_t length = static_cast<size_t>(0.20f * static_cast<float>(sampleRate));
					for (size_t i = 0; i < length && frame + i < kickEnvelope.size(); ++i)
					{
						const float decay = std::exp(-static_cast<float>(i) / (kickDecaySeconds * static_cast<float>(sampleRate)));
						kickEnvelope[frame + i] = std::max(kickEnvelope[frame + i], decay);
					}
				}

				if (step < song.Snare.size())
				{
					const float snare = song.Snare[step];
					if (snare > 0.0f && weight && !intro)
						AddSnare(drums, frame, sampleRate, kSnareGain * snare * song.Mix.Snare, 0x1234u + bar * 32u + step);
				}

				if (step < song.Hat.size())
				{
					const float hat = song.Hat[step];
					if (hat > 0.0f)
						AddHat(drums, frame, sampleRate, kHatGain * hat * hatScale * song.Mix.Hat,
							step + 2 == song.Hat.size(), 0x5678u + bar * 32u + step);
				}
			}
		}

		if (bar >= song.Chords.size())
			continue;

		const ThemeChord& chord = song.Chords[bar];
		const int16_t root = chord.Root;

		// The bridge keeps the pads and thins the bass right back, so the drum
		// drop is felt rather than merely heard.
		const float bedScale = bridge ? 0.55f : 1.0f;
		const bool bassIn = !bridge && !intro && !lastBar;

		if (bassIn)
		{
			// Driving eighth-note bass, with the octave-up lift on the last
			// eighth of the bar that walks into the next.
			for (uint32_t eighth = 0; eighth < eighthsPerBar; ++eighth)
			{
				const int16_t note = static_cast<int16_t>(root + (eighth + 1 == eighthsPerBar ? 12 : 0));
				AddBassNote(music, sampleRate,
					barStart + static_cast<size_t>(static_cast<float>(eighth * 2) * framesPerSixteenth),
					1.0f, note, (bridge ? bedScale * 0.85f : bedScale) * song.Mix.Bass);
			}
		}

		AddPadChord(music, sampleRate, barStart, static_cast<float>(eighthsPerBar), chord.Root,
			chord.Third, bedScale * song.Mix.Pad);

		// The chorus arps: eighth blips picking out the chord tones.
		if (named(bar, "Chorus"))
		{
			const int16_t tones[3] = { chord.Root, chord.Third, static_cast<int16_t>(chord.Root + 7) };
			for (uint32_t s = 0; s < eighthsPerBar; ++s)
				AddArpNote(music, sampleRate,
					barStart + static_cast<size_t>(static_cast<float>(s * 2) * framesPerSixteenth),
					static_cast<int16_t>(tones[s % 3] + 24), song.Mix.Arp);
		}

		// --- The melody ---
		for (const ThemeMelodyNote* note : melodyInBar[bar])
		{
			const size_t frame = barStart
				+ static_cast<size_t>(static_cast<float>(note->OffsetEighths) * 0.5f * framesPerUnit);
			AddLeadNote(music, sampleRate, frame,
				static_cast<float>(note->DurationEighths) * 0.5f, note->Midi, song.Mix.Lead);
		}
	}

	// --- Mix ---
	//
	// Side-chain first: the music bus rides a follower that snaps down at each
	// kick and eases back up, which is the pumping the reference mixes have.
	// The drums are summed dry on top.
	float* musicData = music.Data();
	const float* drumsData = drums.Data();

	const float rise = 1.0f / std::max(kDuckAttack * static_cast<float>(sampleRate), 1.0f);
	const float fall = 1.0f / std::max(kDuckRelease * static_cast<float>(sampleRate), 1.0f);

	float follower = 1.0f;
	std::vector<float> out(frames * 2, 0.0f);

	for (size_t i = 0; i < frames; ++i)
	{
		const float target = 1.0f - kDuckDepth * kickEnvelope[i];
		const float rate = target > follower ? rise : fall;
		follower += (target - follower) * rate;

		const float master = kMasterGain * song.ArrangementGain;
		out[i * 2 + 0] = (musicData[i * 2 + 0] * follower + drumsData[i * 2 + 0]) * master;
		out[i * 2 + 1] = (musicData[i * 2 + 1] * follower + drumsData[i * 2 + 1]) * master;
	}

	return out;
}

}  // namespace Tetris