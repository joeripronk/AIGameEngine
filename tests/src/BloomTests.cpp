#include "fgpch.h"

#include "Forge/Renderer/Bloom.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>

using namespace Forge;

TEST_CASE("Bloom defaults match the published chain", "[renderer][bloom]")
{
	const BloomParams params;
	CHECK(params.Intensity == Catch::Approx(0.05f));
	CHECK(params.Threshold == Catch::Approx(1.0f));  // highlights only
	CHECK(params.Knee == Catch::Approx(0.5f));
	CHECK(params.Scale == Catch::Approx(1.0f));
}

TEST_CASE("The soft-threshold luminance filter passes highlights and rejects the rest", "[renderer][bloom]")
{
	const BloomParams params;

	// Below threshold: no contribution at all. This is the whole point of
	// the threshold - walls do not glow.
	for (float luma : { 0.0f, 0.25f, 0.5f, 0.9f, 1.0f })
		CHECK(BloomLumaWeight(luma, params) == 0.0f);

	// Above threshold: a real, positive, finite weight.
	float peak = 0.0f;
	for (float luma = 1.1f; luma <= 4.0f; luma += 0.1f)
	{
		const float weight = BloomLumaWeight(luma, params);
		CHECK(weight > 0.0f);
		CHECK(std::isfinite(weight));
		peak = std::max(peak, weight);
	}
	CHECK(peak > 0.0f);

	// The weight stays in range: it is a per-texel multiplier, so a value
	// over 1 means the filter is amplifying, which is the bright-texel
	// crawl the luminance normalization exists to prevent.
	for (float luma : { 1.01f, 1.5f, 2.0f, 5.0f, 50.0f, 1000.0f })
	{
		const float weight = BloomLumaWeight(luma, params);
		CHECK(weight >= 0.0f);
		CHECK(weight <= 1.0f);
		CHECK(std::isfinite(weight));
	}

	// The filter saturates: the glow rises just over the threshold and
	// then levels off (the normalized weight approaches the knee limit and
	// falls back), so a lamp at 10x threshold does not project 10x the
	// glow. Unbounded growth with luminance here would be the crawl, not
	// the effect.
	CHECK(BloomLumaWeight(50.0f, params) < peak);
	CHECK(BloomLumaWeight(1000.0f, params) < BloomLumaWeight(2.0f, params));

	// Scale multiplies the weight.
	BloomParams scaled = params;
	scaled.Scale = 2.0f;
	CHECK(Catch::Approx(BloomLumaWeight(2.0f, scaled)) == 2.0f * BloomLumaWeight(2.0f, params));
}

TEST_CASE("Knee controls the transition at the threshold", "[renderer][bloom]")
{
	BloomParams hard;
	hard.Knee = 0.0f;   // knee = threshold * 0 = 0: the soft term vanishes, glow off
	BloomParams soft;
	soft.Knee = 0.5f;

	// A zero knee zeroes the filter (the denominator and numerator
	// coincide), which is the "no transition" degenerate case - it must
	// not produce NaN, negative weights, or a jump.
	for (float luma : { 1.0f, 1.5f, 2.0f, 10.0f })
	{
		const float weight = BloomLumaWeight(luma, hard);
		CHECK(std::isfinite(weight));
		CHECK(weight >= 0.0f);
		CHECK(weight < 1e-3f);
	}

	// With a knee, just above the threshold contributes: the fade-in
	// exists precisely so highlights coming over the threshold ramp
	// instead of switching, which strobes on moving geometry.
	CHECK(BloomLumaWeight(1.1f, soft) > 0.0f);

	// Sweeping the knee is a continuous control: more knee, more
	// contribution near the threshold, moving smoothly.
	float previous = -1.0f;
	for (float knee = 0.0f; knee <= 1.0f; knee += 0.1f)
	{
		BloomParams params;
		params.Knee = knee;
		const float weight = BloomLumaWeight(1.5f, params);
		CHECK(std::isfinite(weight));
		CHECK(weight >= previous - 1e-5f);  // monotone non-decreasing as the knee softens
		previous = weight;
	}
}

TEST_CASE("The downsample kernel is a 13-tap box that covers the destination footprint", "[renderer][bloom]")
{
	const std::vector<glm::vec2> offsets = BloomKernel::DownsampleOffsets();

	REQUIRE(offsets.size() == 13u);

	// Every tap inside the footprint of a half-res texel on the full-res
	// source (the 2x2 block plus its immediate ring): |offset| <= 1.5 texels.
	for (const glm::vec2& o : offsets)
	{
		CHECK(std::abs(o.x) <= 1.5f);
		CHECK(std::abs(o.y) <= 1.5f);
	}

	// The centre tap exists: the destination texel always samples its own
	// footprint centre.
	const bool hasCentre = std::any_of(offsets.begin(), offsets.end(),
		[](const glm::vec2& o) { return o.x == 0.0f && o.y == 0.0f; });
	CHECK(hasCentre);

	// No duplicate taps: each tap has to contribute a distinct sample.
	for (size_t i = 0; i < offsets.size(); ++i)
		for (size_t j = i + 1; j < offsets.size(); ++j)
			CHECK(offsets[i] != offsets[j]);
}

TEST_CASE("The blur kernel is 16 distinct golden-spiral taps in the unit disc", "[renderer][bloom]")
{
	const std::vector<glm::vec2> offsets = BloomKernel::BlurOffsets();

	REQUIRE(offsets.size() == 16u);

	for (const glm::vec2& o : offsets)
	{
		CHECK(std::isfinite(o.x));
		CHECK(std::isfinite(o.y));
		// Inside the unit disc: the pass multiplies by texel size and the
		// iteration radius, so a tap beyond 1.0 reaches past its intended
		// disc.
		CHECK(std::sqrt(o.x * o.x + o.y * o.y) <= 1.0f);
	}

	// Distinct taps: 16 identical offsets would be a 1-tap blur at 16x cost.
	for (size_t i = 0; i < offsets.size(); ++i)
		for (size_t j = i + 1; j < offsets.size(); ++j)
		{
			const glm::vec2 d = offsets[i] - offsets[j];
			CHECK(std::sqrt(d.x * d.x + d.y * d.y) > 1e-4f);
		}

	// The spiral fills outward: tap radii increase with the index, which is
	// what makes the disc even rather than ringed.
	float previousRadius = -1.0f;
	for (const glm::vec2& o : offsets)
	{
		const float radius = std::sqrt(o.x * o.x + o.y * o.y);
		CHECK(radius > previousRadius);
		previousRadius = radius;
	}

	// Coverage: the taps spread over all directions - the mean offset is
	// near the origin, i.e. no directional bias to smear the glow sideways.
	glm::vec2 sum(0.0f);
	for (const glm::vec2& o : offsets)
		sum += o;
	CHECK(std::sqrt(sum.x * sum.x + sum.y * sum.y) < 0.5f);
}
