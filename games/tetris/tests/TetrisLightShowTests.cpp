#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TetrisBoardView.h"
#include "TetrisLightShow.h"

#include "Forge/Core/Log.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"

#include <cmath>
#include <string>

using namespace Tetris;

namespace {

// A colour's own brightness: how much light it carries, whatever hue it is.
float Brightness(const glm::vec3& color)
{
	return std::max({ color.r, color.g, color.b });
}

// Whether two colours are different. Catch2 cannot decompose a chain of
// comparisons, so the answer is worked out here and asked for as one.
bool Differs(const glm::vec3& a, const glm::vec3& b)
{
	return std::abs(a.r - b.r) > 0.001f
		|| std::abs(a.g - b.g) > 0.001f
		|| std::abs(a.b - b.b) > 0.001f;
}

}  // namespace

TEST_CASE("The lamps hang across the top of the playfield", "[tetris][light]")
{
	const glm::vec3 centre = BoardView::BoardCentre();
	const glm::vec3 size = BoardView::BoardSize();

	glm::vec3 first = LightShow::LampPosition(0);
	glm::vec3 last = LightShow::LampPosition(LightShow::kLightCount - 1);

	// The rig is a row, and the row is level: the lamps hang at one height.
	REQUIRE(first.y == Catch::Approx(last.y));

	// Above the middle of the playfield and in front of it: a lamp behind
	// the playfield lights the backs of the blocks, and the camera sees the
	// fronts.
	REQUIRE(first.y > centre.y);
	REQUIRE(first.z > centre.z);

	// The row is spread across the playfield, not stacked on one spot.
	REQUIRE(last.x > first.x);
	REQUIRE(first.x > centre.x - size.x * 0.5f);
	REQUIRE(last.x < centre.x + size.x * 0.5f);
}

TEST_CASE("The show is a pure function of the clock", "[tetris][light]")
{
	// The same moment gives the same show, every time: a show that is a
	// function of anything else cannot be reproduced.
	const LightShow::Sample a = LightShow::SampleAt(3.25f, 2);
	const LightShow::Sample b = LightShow::SampleAt(3.25f, 2);
	REQUIRE(a.Color.r == Catch::Approx(b.Color.r));
	REQUIRE(a.Color.g == Catch::Approx(b.Color.g));
	REQUIRE(a.Color.b == Catch::Approx(b.Color.b));
	REQUIRE(a.Intensity == Catch::Approx(b.Intensity));
	REQUIRE(a.Position.x == Catch::Approx(b.Position.x));
	REQUIRE(a.Position.y == Catch::Approx(b.Position.y));
	REQUIRE(a.Position.z == Catch::Approx(b.Position.z));
}

TEST_CASE("The show is coloured and it moves", "[tetris][light]")
{
	// Every lamp carries a colour of the spectrum: the show is RGB, not a
	// dimming of white.
	for (uint32_t index = 0; index < LightShow::kLightCount; ++index)
	{
		INFO("lamp " << index);

		const LightShow::Sample sample = LightShow::SampleAt(1.5f, index);
		REQUIRE(Brightness(sample.Color) == Catch::Approx(1.0f));
		REQUIRE(sample.Intensity > 0.0f);
	}

	// The hue walks: the same lamp is a different colour a moment later.
	const LightShow::Sample early = LightShow::SampleAt(0.0f, 0);
	const LightShow::Sample later = LightShow::SampleAt(2.0f, 0);
	REQUIRE(Differs(early.Color, later.Color));

	// The lamps are out of step with each other: a row of lamps all at the
	// same colour is a strip light, not a show.
	const LightShow::Sample first = LightShow::SampleAt(0.4f, 0);
	const LightShow::Sample second = LightShow::SampleAt(0.4f, 1);
	REQUIRE(Differs(first.Color, second.Color));
}

TEST_CASE("The show holds for as long as it is asked to", "[tetris][light]")
{
	// The show's brightness stays in a sane band: a lamp that goes out for
	// a frame is a flicker, and a lamp that blows out is a white screen.
	float lowest = 1e9f;
	float highest = -1e9f;

	for (int step = 0; step < 600; ++step)
	{
		const float time = static_cast<float>(step) * 0.05f;
		for (uint32_t index = 0; index < LightShow::kLightCount; ++index)
		{
			const float intensity = LightShow::SampleAt(time, index).Intensity;
			lowest = std::min(lowest, intensity);
			highest = std::max(highest, intensity);
		}
	}

	REQUIRE(lowest > 0.0f);
	REQUIRE(highest < 100.0f);
}

TEST_CASE("The show drives the scene's own lamps", "[tetris][light][scene]")
{
	Forge::Scene scene;

	LightShow show;
	REQUIRE(show.Initialize(&scene));

	// The show made its lamps, and they are point lights: a lamp that is
	// not a point light does not fall off, and a light show of lights that
	// do not fall off is a floodlight.
	for (uint32_t index = 0; index < LightShow::kLightCount; ++index)
	{
		Forge::Entity light = scene.FindEntityByName(LightShow::LightName(index));
		REQUIRE(light);
		REQUIRE(light.HasComponent<Forge::LightComponent>());
		REQUIRE(light.GetComponent<Forge::LightComponent>().Type == Forge::LightComponent::Type::Point);
	}

	// The show moves them: the lamps drift, so the pools of light move.
	const glm::vec3 before = scene.FindEntityByName(LightShow::LightName(0))
		.GetComponent<Forge::TransformComponent>().Translation;
	show.Update(1.0f);
	const glm::vec3 after = scene.FindEntityByName(LightShow::LightName(0))
		.GetComponent<Forge::TransformComponent>().Translation;
	REQUIRE(before.x != Catch::Approx(after.x));

	// The show off: the lamps are dark, not gone. A lamp that is taken away
	// when the show stops is a light that was never really there.
	show.SetEnabled(false);
	show.Update(0.1f);
	REQUIRE(scene.FindEntityByName(LightShow::LightName(0))
		.GetComponent<Forge::LightComponent>().Intensity == Catch::Approx(0.0f));
	REQUIRE(scene.FindEntityByName(LightShow::LightName(0)));

	// And the show back on.
	show.SetEnabled(true);
	show.Update(0.1f);
	REQUIRE(scene.FindEntityByName(LightShow::LightName(0))
		.GetComponent<Forge::LightComponent>().Intensity > 0.0f);

	show.Shutdown();
}
