#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Forge/Core/Tween.h"

using namespace Forge;

TEST_CASE("Every curve starts at zero and ends at one", "[core][tween]")
{
	const TweenCurve curves[] = {
		TweenCurve::Linear, TweenCurve::Sine, TweenCurve::Quad, TweenCurve::Cubic,
		TweenCurve::Quart, TweenCurve::Quint, TweenCurve::Expo, TweenCurve::Circ,
		TweenCurve::Back, TweenCurve::Bounce, TweenCurve::Elastic,
	};

	for (TweenCurve curve : curves)
	{
		for (TweenEase ease : { TweenEase::In, TweenEase::Out, TweenEase::InOut, TweenEase::OutIn })
		{
			REQUIRE(EvaluateCurve(curve, ease, 0.0f) == Catch::Approx(0.0f).margin(0.001f));
			REQUIRE(EvaluateCurve(curve, ease, 1.0f) == Catch::Approx(1.0f).margin(0.001f));
		}
	}
}

TEST_CASE("A point outside the curve is clamped", "[core][tween]")
{
	// A tween that is asked about a time before it started or after it ended
	// answers with the value it holds there.
	REQUIRE(EvaluateCurve(TweenCurve::Quad, TweenEase::In, -1.0f) == 0.0f);
	REQUIRE(EvaluateCurve(TweenCurve::Quad, TweenEase::In, 2.0f) == 1.0f);
}

TEST_CASE("The linear curve is the straight line", "[core][tween]")
{
	REQUIRE(EvaluateCurve(TweenCurve::Linear, TweenEase::In, 0.5f) == Catch::Approx(0.5f));
	REQUIRE(EvaluateCurve(TweenCurve::Linear, TweenEase::Out, 0.5f) == Catch::Approx(0.5f));
	REQUIRE(EvaluateCurve(TweenCurve::Linear, TweenEase::InOut, 0.5f) == Catch::Approx(0.5f));
}

TEST_CASE("An 'in' curve starts slowly and an 'out' curve ends slowly", "[core][tween]")
{
	// The quad curve: t^2 in, 1-(1-t)^2 out.
	REQUIRE(EvaluateCurve(TweenCurve::Quad, TweenEase::In, 0.5f) == Catch::Approx(0.25f));
	REQUIRE(EvaluateCurve(TweenCurve::Quad, TweenEase::Out, 0.5f) == Catch::Approx(0.75f));

	// The in-out curve is slow at both ends: the middle is halfway, and the
	// quarters are not.
	REQUIRE(EvaluateCurve(TweenCurve::Quad, TweenEase::InOut, 0.5f) == Catch::Approx(0.5f));
	REQUIRE(EvaluateCurve(TweenCurve::Quad, TweenEase::InOut, 0.25f) < 0.25f);
	REQUIRE(EvaluateCurve(TweenCurve::Quad, TweenEase::InOut, 0.75f) > 0.75f);
}

TEST_CASE("A tween moves its value to the target", "[core][tween]")
{
	float written = -1.0f;
	Tween tween(0.0f, 10.0f, 1.0f, TweenCurve::Linear, TweenEase::InOut,
		[&written](float value) { written = value; });

	// Halfway through, the value is halfway.
	REQUIRE(tween.Update(0.5f));
	REQUIRE(written == Catch::Approx(5.0f));
	REQUIRE(tween.GetProgress() == Catch::Approx(0.5f));

	// The end of the tween is the target.
	REQUIRE_FALSE(tween.Update(0.5f));
	REQUIRE(written == Catch::Approx(10.0f));
	REQUIRE(tween.IsFinished());
	REQUIRE(tween.GetValue() == Catch::Approx(10.0f));

	// A finished tween does not move any more.
	REQUIRE_FALSE(tween.Update(1.0f));
	REQUIRE(written == Catch::Approx(10.0f));
}

TEST_CASE("A tween with no duration is over before it starts", "[core][tween]")
{
	float written = -1.0f;
	Tween tween(0.0f, 5.0f, 0.0f, TweenCurve::Linear, TweenEase::In,
		[&written](float value) { written = value; });

	// The value it moves to is the one it holds.
	REQUIRE(tween.IsFinished());
	REQUIRE(written == Catch::Approx(5.0f));
}

TEST_CASE("A cancelled tween stops where it is", "[core][tween]")
{
	float written = -1.0f;
	Tween tween(0.0f, 10.0f, 1.0f, TweenCurve::Linear, TweenEase::InOut,
		[&written](float value) { written = value; });

	REQUIRE(tween.Update(0.25f));
	REQUIRE(written == Catch::Approx(2.5f));

	tween.Cancel();
	REQUIRE(tween.IsCancelled());

	// A cancelled tween does not jump to its target.
	REQUIRE_FALSE(tween.Update(1.0f));
	REQUIRE(written == Catch::Approx(2.5f));
}

TEST_CASE("The tween service runs the tweens and drops the finished ones", "[core][tween]")
{
	Tweens::Clear();
	REQUIRE(Tweens::Count() == 0);

	float first = 0.0f;
	float second = 0.0f;
	Tweens::Add(Tween(0.0f, 1.0f, 1.0f, TweenCurve::Linear, TweenEase::In,
		[&first](float value) { first = value; }));
	Tweens::Add(Tween(0.0f, 2.0f, 2.0f, TweenCurve::Linear, TweenEase::In,
		[&second](float value) { second = value; }));

	REQUIRE(Tweens::Count() == 2);

	Tweens::Update(0.5f);
	REQUIRE(first == Catch::Approx(0.5f));
	REQUIRE(second == Catch::Approx(0.5f));
	REQUIRE(Tweens::Count() == 2);

	// The first tween is done after a second; the second one is not.
	Tweens::Update(0.5f);
	REQUIRE(first == Catch::Approx(1.0f));
	REQUIRE(second == Catch::Approx(1.0f));
	REQUIRE(Tweens::Count() == 1);

	// And the one that is left runs out too.
	Tweens::Update(1.0f);
	REQUIRE(second == Catch::Approx(2.0f));
	REQUIRE(Tweens::Count() == 0);

	Tweens::Clear();
}
