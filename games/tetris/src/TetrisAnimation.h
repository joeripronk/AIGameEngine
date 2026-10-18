#pragma once

#include <algorithm>
#include <cmath>

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

// How hard the ground shakes for a block that hit the board at `cellsPerSecond`
// cells per second. A gravity lock at level 1 falls at a little over one cell a
// second and is meant to be felt, not seen; a drop from the top saturates it.
// Clamped, because a 60 fps frame divides by a small dt and an unclamped
// amplitude turns into a camera that has left the playfield.
inline float ShakeAmplitudeForSpeed(float cellsPerSecond, float maxAmplitude)
{
	constexpr float kFullShakeSpeed = 260.0f;
	if (cellsPerSecond <= 0.0f || maxAmplitude <= 0.0f)
		return 0.0f;
	return maxAmplitude * Clamp01(cellsPerSecond / kFullShakeSpeed);
}

}  // namespace Tetris
