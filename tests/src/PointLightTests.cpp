#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
using Catch::Approx;

#include "Forge/Renderer/PointLights.h"

#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"

#include <vector>

namespace {

// A lamp at a place, with a brightness.
Forge::PointLight MakeLight(const glm::vec3& position, float intensity)
{
	Forge::PointLight light;
	light.Position = position;
	light.Color = glm::vec3(1.0f);
	light.Intensity = intensity;
	return light;
}

}  // namespace

TEST_CASE("A scene's point lights are found", "[renderer][lights]")
{
	Forge::Scene scene;

	// A lamp, where the scene says it is.
	Forge::Entity lamp = scene.CreateEntity("Lamp");
	lamp.GetComponent<Forge::TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
	auto& light = lamp.AddComponent<Forge::LightComponent>();
	light.Type = Forge::LightComponent::Type::Point;
	light.Color = { 0.25f, 0.5f, 0.75f };
	light.Intensity = 3.0f;

	// The key light is not a lamp: it lights everything at once, and it is
	// the one the shadow map is built from.
	Forge::Entity key = scene.CreateEntity("Sun");
	key.AddComponent<Forge::LightComponent>().Type = Forge::LightComponent::Type::Directional;

	const std::vector<Forge::PointLight> lights = Forge::CollectPointLights(scene);

	REQUIRE(lights.size() == 1);
	REQUIRE(lights[0].Position == glm::vec3(1.0f, 2.0f, 3.0f));
	REQUIRE(lights[0].Color == glm::vec3(0.25f, 0.5f, 0.75f));
	REQUIRE(lights[0].Intensity == Approx(3.0f));
}

TEST_CASE("Only lamps are lamps", "[renderer][lights]")
{
	Forge::Scene scene;

	// The key light is the sun: it lights everything at once, and it is the
	// one the shadow map is built from. It is not a lamp.
	Forge::Entity key = scene.CreateEntity("Sun");
	key.AddComponent<Forge::LightComponent>().Type = Forge::LightComponent::Type::Directional;

	REQUIRE(Forge::CollectPointLights(scene).empty());

	// A lamp at the origin is a lamp at the origin: the world origin is a
	// place, and a lamp there is a lamp there.
	Forge::Entity lamp = scene.CreateEntity("Lamp");
	lamp.AddComponent<Forge::LightComponent>().Type = Forge::LightComponent::Type::Point;

	const std::vector<Forge::PointLight> lights = Forge::CollectPointLights(scene);
	REQUIRE(lights.size() == 1);
	REQUIRE(lights[0].Position == glm::vec3(0.0f, 0.0f, 0.0f));
}

TEST_CASE("The brightest lights are the ones a frame is drawn with", "[renderer][lights]")
{
	std::vector<Forge::PointLight> candidates;
	candidates.push_back(MakeLight({ 0.0f, 0.0f, 0.0f }, 0.5f));
	candidates.push_back(MakeLight({ 1.0f, 0.0f, 0.0f }, 4.0f));
	candidates.push_back(MakeLight({ 2.0f, 0.0f, 0.0f }, 1.0f));

	const Forge::PointLightSet selected = Forge::SelectPointLights(candidates);

	REQUIRE(selected.Count == 3);

	// The brightest first: when there is not room for them all, the ones
	// doing the most are the ones that stay.
	REQUIRE(selected.Lights[0].Intensity == Approx(4.0f));
	REQUIRE(selected.Lights[1].Intensity == Approx(1.0f));
	REQUIRE(selected.Lights[2].Intensity == Approx(0.5f));
}

TEST_CASE("A scene with more lamps than the shader can take keeps the brightest", "[renderer][lights]")
{
	std::vector<Forge::PointLight> candidates;
	for (uint32_t i = 0; i < Forge::kMaxPointLights + 5; ++i)
		candidates.push_back(MakeLight({ static_cast<float>(i), 0.0f, 0.0f }, static_cast<float>(i)));

	const Forge::PointLightSet selected = Forge::SelectPointLights(candidates);

	REQUIRE(selected.Count == Forge::kMaxPointLights);
	REQUIRE(selected.Lights[0].Intensity == Approx(12.0f));
	REQUIRE(selected.Lights[Forge::kMaxPointLights - 1].Intensity == Approx(5.0f));
}

TEST_CASE("Lamps of the same brightness keep the order they were given", "[renderer][lights]")
{
	std::vector<Forge::PointLight> candidates;
	candidates.push_back(MakeLight({ 1.0f, 0.0f, 0.0f }, 2.0f));
	candidates.push_back(MakeLight({ 2.0f, 0.0f, 0.0f }, 2.0f));
	candidates.push_back(MakeLight({ 3.0f, 0.0f, 0.0f }, 2.0f));

	const Forge::PointLightSet selected = Forge::SelectPointLights(candidates);

	// A stable order: two lamps of the same brightness must not swap places
	// from frame to frame, or the scene flickers between two arrangements of
	// the same show.
	REQUIRE(selected.Count == 3);
	REQUIRE(selected.Lights[0].Position.x == Approx(1.0f));
	REQUIRE(selected.Lights[1].Position.x == Approx(2.0f));
	REQUIRE(selected.Lights[2].Position.x == Approx(3.0f));
}

TEST_CASE("A scene with no lamps is a scene with no lamps", "[renderer][lights]")
{
	Forge::Scene scene;

	REQUIRE(Forge::CollectPointLights(scene).empty());

	const Forge::PointLightSet selected = Forge::SelectPointLights(Forge::CollectPointLights(scene));
	REQUIRE(selected.Count == 0);
}
