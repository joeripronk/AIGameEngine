#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace Tetris {

// The maths behind how a block *moves*, as opposed to where the rules say it
// is. The rules stay instantaneous - they are the authoritative, deterministic
// state the tests drive - and everything here is what presenting that state
// looks like. Pure arithmetic, so every property below is assertable with no
// window and no graphics device.

inline float Clamp01(float t)
{
	return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

// Accelerate, then decelerate: starts and ends at rest. The ordinary curve for
// a block moving one cell to its next position.
inline float EaseInOut(float t)
{
	const float x = Clamp01(t);
	return x * x * (3.0f - 2.0f * x);
}

// Constant rate. A piece under gravity falls at a constant rate, so its
// presentation has to be constant too: easing each cell of a steady fall makes
// the block accelerate and brake once per row, which reads as a stutter at
// exactly the frequency the fall is supposed to be smooth at.
inline float GlideRate(float t)
{
	return Clamp01(t);
}

// A lightning strike: also starts and ends at rest, but the attack is far
// steeper than the settle. Velocity is t * exp(-k t), so it accelerates away
// from zero and decays back to it; integrating and normalising gives the
// position. The same shape for a 1-cell move and a 19-cell drop, which is why
// the strike reads as one gesture rather than as a speed setting.
inline float StrikeEase(float t)
{
	constexpr float k = 6.0f;
	const float x = Clamp01(t);
	const float norm = 1.0f - std::exp(-k) * (1.0f + k);
	return (1.0f - std::exp(-k * x) * (1.0f + k * x)) / norm;
}

// Seconds a strike takes to cover `cells` cells. Distance has to buy time: a
// 19-cell drop that finishes in the time of a 1-cell move is a teleport with a
// curve painted on it, not an acceleration.
inline float StrikeDuration(float cells)
{
	constexpr float kBase = 0.055f;
	constexpr float kPerCell = 0.011f;
	constexpr float kMax = 0.26f;
	const float distance = cells < 0.0f ? -cells : cells;
	return std::min(kBase + kPerCell * distance, kMax);
}

// Seconds a block takes to slide `cells` cells under EaseInOut.
inline float SlideDuration(float cells)
{
	constexpr float kBase = 0.055f;
	constexpr float kPerCell = 0.012f;
	constexpr float kMax = 0.16f;
	const float distance = cells < 0.0f ? -cells : cells;
	return std::min(kBase + kPerCell * distance, kMax);
}

// How bright a cleared row is at fraction `t` of its clear. The row lights up
// as it goes out and is back to its own colour when the blocks are gone: the
// peak is in the middle, where the player is looking, and zero at both ends so
// a row that has finished vanishing cannot be glowing, and one that has not
// started cannot be lit before anything has happened.
inline float LineClearFlash(float t)
{
	constexpr float kPi = 3.14159265358979323846f;
	const float s = std::sin(kPi * Clamp01(t));
	// sin(pi) is a hair under zero in float, and a negative flash weight would
	// mix the row *away* from white, past black, and wrap in the shader.
	return s < 0.0f ? 0.0f : s;
}

// The flash is white, and a row's own colour is the starting point: the mix
// factor is the flash weight, so the emissive term of a clearing block is
// `mix(color, white, LineClearFlash(t))`.
inline glm::vec3 FlashTowardWhite(const glm::vec3& color, float t)
{
	const float w = LineClearFlash(t);
	return color + (glm::vec3(1.0f) - color) * w;
}

// The same flash toward an arbitrary colour. A melting block burns toward the
// fire's orange, not white, so the glow is the colour of what burned it.
inline glm::vec3 FlashTowardColor(const glm::vec3& color, const glm::vec3& target, float t)
{
	const float w = LineClearFlash(t);
	return color + (target - color) * w;
}

// A fire block flashes: a pulse in [0,1] at a fixed rate, peaking and falling
// on a smooth curve so it reads as a throbbing glow, not a strobe. Drives the
// emissive intensity of the falling fire piece.
inline float FireFlashPulse(float seconds)
{
	constexpr float kPi = 3.14159265358979323846f;
	constexpr float kFireFlashHz = 3.0f;
	const float s = std::sin(2.0f * kPi * kFireFlashHz * seconds);
	return 0.5f + 0.5f * s;
}

// Shake amplitude that grows with the number of lines cleared and with how
// fast the blocks were moving. More lines removed -> larger; faster impact ->
// even bigger. Clamped to maxAmplitude.
inline float ShakeAmplitudeForClear(int linesCleared, float cellsPerSecond, float maxAmplitude)
{
	if (maxAmplitude <= 0.0f)
		return 0.0f;
	if (linesCleared <= 0 && cellsPerSecond <= 0.0f)
		return 0.0f;

	constexpr float kFullShakeSpeed = 260.0f;
	const float lineContribution = 0.25f * static_cast<float>(linesCleared > 0 ? linesCleared : 0);
	const float speedContribution = cellsPerSecond > 0.0f ? (cellsPerSecond / kFullShakeSpeed) : 0.0f;
	return maxAmplitude * Clamp01(lineContribution + speedContribution);
}

// How hard the ground shakes for a block that hit the board at `cellsPerSecond`
// cells per second (no lines cleared).
inline float ShakeAmplitudeForSpeed(float cellsPerSecond, float maxAmplitude)
{
	return ShakeAmplitudeForClear(0, cellsPerSecond, maxAmplitude);
}

// Aliases covering the naming a caller might reach for.
inline float ShakeAmplitudeForLines(int linesCleared, float cellsPerSecond, float maxAmplitude)
{
	return ShakeAmplitudeForClear(linesCleared, cellsPerSecond, maxAmplitude);
}

inline float LineClearShakeAmplitude(int linesCleared, float cellsPerSecond, float maxAmplitude)
{
	return ShakeAmplitudeForClear(linesCleared, cellsPerSecond, maxAmplitude);
}

inline float ShakeAmplitude(int linesCleared, float cellsPerSecond, float maxAmplitude)
{
	return ShakeAmplitudeForClear(linesCleared, cellsPerSecond, maxAmplitude);
}

// Overload keeping the original two-argument signature working, with an
// optional leading line count for callers that pass (lines, speed, max).
inline float ShakeAmplitudeForSpeed(float cellsPerSecond, int linesCleared, float maxAmplitude)
{
	return ShakeAmplitudeForClear(linesCleared, cellsPerSecond, maxAmplitude);
}

}  // namespace Tetris
