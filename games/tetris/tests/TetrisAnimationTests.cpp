#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TetrisAnimation.h"

#include <cmath>
#include <string>

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


TEST_CASE("Reward flash pulses in [0,1]", "[tetris][animation][reward]")
{
	for (float t = 0.0f; t <= 2.0f; t += 0.01f)
	{
		const float p = RewardFlashPulse(t);
		REQUIRE(p >= 0.0f);
		REQUIRE(p <= 1.0f);
	}
	// It is a pulse, not a constant: it reaches both extremes.
	REQUIRE(RewardFlashPulse(0.0f) < 1.0f);
	REQUIRE(RewardFlashPulse(1.0f) >= 0.0f);
}

TEST_CASE("Reward shake, font scale and duration all grow with lines cleared", "[tetris][animation][reward]")
{
	const float amp1 = RewardShakeAmplitude(1, 100.0f);
	const float amp2 = RewardShakeAmplitude(2, 100.0f);
	const float amp3 = RewardShakeAmplitude(3, 100.0f);
	const float amp4 = RewardShakeAmplitude(4, 100.0f);
	REQUIRE(amp1 > 0.0f);
	REQUIRE(amp2 > amp1);
	REQUIRE(amp3 > amp2);
	REQUIRE(amp4 > amp3);
	REQUIRE(amp4 == Catch::Approx(100.0f));  // four lines is the full amplitude

	REQUIRE(RewardFontScale(1) < RewardFontScale(2));
	REQUIRE(RewardFontScale(2) < RewardFontScale(3));
	REQUIRE(RewardFontScale(3) < RewardFontScale(4));

	REQUIRE(RewardDuration(1) < RewardDuration(2));
	REQUIRE(RewardDuration(2) < RewardDuration(4));

	// Nothing cleared shakes nothing.
	REQUIRE(RewardShakeAmplitude(0, 100.0f) == 0.0f);
}

TEST_CASE("Combo labels name the chain and escalate as it grows", "[tetris][animation][reward]")
{
	// A lone clear is not a combo: the toast keeps its line label.
	REQUIRE(ComboLabelForChain(0) == nullptr);
	REQUIRE(ComboLabelForChain(1) == nullptr);

	REQUIRE(std::string(ComboLabelForChain(2)) == "Nice Combo");
	REQUIRE(std::string(ComboLabelForChain(3)) == "Nice Combo");
	REQUIRE(std::string(ComboLabelForChain(4)) == "Great Combo");
	REQUIRE(std::string(ComboLabelForChain(5)) == "Great Combo");
	REQUIRE(std::string(ComboLabelForChain(6)) == "Amazing Combo");
	REQUIRE(std::string(ComboLabelForChain(7)) == "Amazing Combo");
	REQUIRE(std::string(ComboLabelForChain(8)) == "Epic Combo");
	REQUIRE(std::string(ComboLabelForChain(64)) == "Epic Combo");
}

TEST_CASE("The camera stands back far enough for the content to fit", "[tetris][animation][view]")
{
	constexpr float kFov = 60.0f;
	constexpr float kHalfHeight = 12.0f;
	constexpr float kHalfWidth = 8.0f;

	// A wide window is held by the height: the content is taller than it is
	// wide, and the camera comes to the same place whatever the width is.
	const float wide = FitDistanceForContent(kHalfHeight, kHalfWidth, 16.0f / 9.0f, kFov);
	const float wider = FitDistanceForContent(kHalfHeight, kHalfWidth, 2.0f, kFov);
	REQUIRE(wide == Catch::Approx(wider));

	// A narrow window is held by the width, and the camera has to come further
	// back: the content is wider than the window is, and cropping the preview
	// off the side of the playfield is not an option.
	const float narrow = FitDistanceForContent(kHalfHeight, kHalfWidth, 0.5f, kFov);
	REQUIRE(narrow > wide);

	// The distance that holds the content is the one where the frustum's own
	// half-height is the content's half-height.
	const float halfFov = std::tan(glm::radians(kFov) * 0.5f);
	REQUIRE(wide * halfFov == Catch::Approx(kHalfHeight));

	// A square window is held by both, and the wider content is what governs.
	const float square = FitDistanceForContent(10.0f, 20.0f, 1.0f, kFov);
	REQUIRE(square * halfFov == Catch::Approx(20.0f));

	// A window of no size is not a shape to fit to, and a field of view of
	// zero has no frustum: neither may divide by zero.
	REQUIRE(FitDistanceForContent(10.0f, 10.0f, 0.0f, kFov) > 0.0f);
	REQUIRE(FitDistanceForContent(10.0f, 10.0f, 1.0f, 0.0f) == 0.0f);
}
