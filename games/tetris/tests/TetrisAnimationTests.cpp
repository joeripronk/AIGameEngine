#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TetrisAnimation.h"

#include <cmath>

using namespace Tetris;

TEST_CASE("Both ease curves start and end at rest", "[tetris][animation]")
{
	REQUIRE(EaseInOut(0.0f) == 0.0f);
	REQUIRE(EaseInOut(1.0f) == 1.0f);
	REQUIRE(StrikeEase(0.0f) == Catch::Approx(0.0f));
	REQUIRE(StrikeEase(1.0f) == Catch::Approx(1.0f));

	// Zero slope at both ends: a block that starts at full speed is a block
	// that was somewhere else one frame ago, which is a teleport with a curve
	// drawn on it. The strike's attack is steep by design, so the bound that
	// matters is the one at the end, where it has to arrive at rest.
	constexpr float kStep = 1.0e-4f;
	REQUIRE(std::abs((EaseInOut(kStep) - EaseInOut(0.0f)) / kStep) < 0.01f);
	REQUIRE(std::abs((EaseInOut(1.0f) - EaseInOut(1.0f - kStep)) / kStep) < 0.01f);
	REQUIRE(std::abs((StrikeEase(kStep) - StrikeEase(0.0f)) / kStep) < 0.01f);
	REQUIRE(std::abs((StrikeEase(1.0f) - StrikeEase(1.0f - kStep)) / kStep) < 0.15f);

	// The last tenth of a strike covers far less ground than an even share
	// would: that is the deceleration, stated without reference to a slope.
	REQUIRE(StrikeEase(1.0f) - StrikeEase(0.9f) < 0.1f);
	REQUIRE(EaseInOut(1.0f) - EaseInOut(0.9f) < 0.1f);
}

TEST_CASE("Both ease curves go forward and stay in range", "[tetris][animation]")
{
	float previousIn = 0.0f, previousStrike = 0.0f;

	for (float t = 0.0f; t <= 1.0f; t += 0.01f)
	{
		const float in = EaseInOut(t);
		const float strike = StrikeEase(t);

		REQUIRE(in >= previousIn);
		REQUIRE(strike >= previousStrike);
		REQUIRE(in >= 0.0f);
		REQUIRE(in <= 1.0f);
		REQUIRE(strike >= 0.0f);
		REQUIRE(strike <= 1.0f);

		previousIn = in;
		previousStrike = strike;
	}

	// Clamped outside the domain: a motion that has overshot must not run back.
	REQUIRE(EaseInOut(-1.0f) == 0.0f);
	REQUIRE(StrikeEase(-1.0f) == Catch::Approx(0.0f));
	REQUIRE(EaseInOut(4.0f) == 1.0f);
	REQUIRE(StrikeEase(4.0f) == Catch::Approx(1.0f));
}

TEST_CASE("The strike attacks harder than the ordinary slide", "[tetris][animation]")
{
	// The whole point of the strike curve: most of the distance is covered
	// early, and the block decelerates into its cell. A symmetric ease over the
	// same time is a move; this is a drop.
	for (float t = 0.05f; t < 0.5f; t += 0.05f)
		REQUIRE(StrikeEase(t) > EaseInOut(t));

	// ... and it hands the rest of the time to the settle.
	REQUIRE(StrikeEase(0.5f) > 0.5f);
}

TEST_CASE("Duration is bought by distance, and bounded", "[tetris][animation]")
{
	REQUIRE(StrikeDuration(0.0f) > 0.0f);
	REQUIRE(StrikeDuration(1.0f) > StrikeDuration(0.0f));
	REQUIRE(StrikeDuration(19.0f) > StrikeDuration(1.0f));
	REQUIRE(StrikeDuration(19.0f) == Catch::Approx(StrikeDuration(40.0f)));

	REQUIRE(SlideDuration(1.0f) > 0.0f);
	REQUIRE(SlideDuration(2.0f) > SlideDuration(1.0f));
	REQUIRE(SlideDuration(40.0f) == Catch::Approx(SlideDuration(80.0f)));

	// Distance is a magnitude: a block moving up animates for as long as one
	// moving down.
	REQUIRE(StrikeDuration(-19.0f) == Catch::Approx(StrikeDuration(19.0f)));
	REQUIRE(SlideDuration(-1.0f) == Catch::Approx(SlideDuration(1.0f)));
}

TEST_CASE("Shake amplitude tracks the falling speed and saturates", "[tetris][animation]")
{
	REQUIRE(ShakeAmplitudeForSpeed(0.0f, 0.4f) == 0.0f);
	REQUIRE(ShakeAmplitudeForSpeed(-12.0f, 0.4f) == 0.0f);
	REQUIRE(ShakeAmplitudeForSpeed(12.0f, 0.0f) == 0.0f);

	REQUIRE(ShakeAmplitudeForSpeed(24.0f, 0.4f) > ShakeAmplitudeForSpeed(12.0f, 0.4f));
	REQUIRE(ShakeAmplitudeForSpeed(48.0f, 0.4f) > ShakeAmplitudeForSpeed(24.0f, 0.4f));

	// Saturation is what keeps a stalled frame - which reports an enormous
	// speed - from throwing the camera off the playfield.
	REQUIRE(ShakeAmplitudeForSpeed(1000.0f, 0.4f) == Catch::Approx(0.4f));
	REQUIRE(ShakeAmplitudeForSpeed(1.0e9f, 0.4f) == Catch::Approx(0.4f));
}

TEST_CASE("The line-clear flash lights up and goes out", "[tetris][animation][clear]")
{
	// A row that has finished vanishing cannot still be glowing, and one that
	// has not started cannot be lit before anything has happened.
	REQUIRE(LineClearFlash(0.0f) == Catch::Approx(0.0f));
	REQUIRE(LineClearFlash(1.0f) == Catch::Approx(0.0f));
	REQUIRE(LineClearFlash(-1.0f) == 0.0f);
	REQUIRE(LineClearFlash(4.0f) == 0.0f);

	// The peak is in the middle, where the player is looking, and the row is
	// brightest there - a flash that only ever brightens never goes out, and
	// one that peaks at the start is a blink, not a clear.
	REQUIRE(LineClearFlash(0.5f) == Catch::Approx(1.0f).margin(0.001f));
	REQUIRE(LineClearFlash(0.25f) < LineClearFlash(0.5f));
	REQUIRE(LineClearFlash(0.75f) < LineClearFlash(0.5f));
	REQUIRE(LineClearFlash(0.25f) == Catch::Approx(LineClearFlash(0.75f)).margin(0.001f));

	// The mix the view uses: the row's own colour at the ends of the clear,
	// white at the peak, and never past white on the way.
	const glm::vec3 red(1.0f, 0.2f, 0.1f);
	REQUIRE(FlashTowardWhite(red, 0.0f) == red);
	REQUIRE(FlashTowardWhite(red, 1.0f) == red);

	const glm::vec3 peak = FlashTowardWhite(red, 0.5f);
	REQUIRE(peak.x == Catch::Approx(1.0f));
	REQUIRE(peak.y == Catch::Approx(1.0f).margin(0.001f));
	REQUIRE(peak.z == Catch::Approx(1.0f).margin(0.001f));

	// Monotonic toward the peak and back: a channel that overshoots white
	// would wrap in the shader.
	for (float t = 0.0f; t <= 1.0f; t += 0.05f)
	{
		const glm::vec3 mixed = FlashTowardWhite(red, t);
		REQUIRE(mixed.x >= red.x);
		REQUIRE(mixed.y >= red.y);
		REQUIRE(mixed.z >= red.z);
		REQUIRE(mixed.x <= 1.0f);
		REQUIRE(mixed.y <= 1.0f);
		REQUIRE(mixed.z <= 1.0f);
	}
}

