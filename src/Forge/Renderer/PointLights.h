#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace Forge {

	class Scene;

	// A point light: a coloured lamp at a place in the world.
	//
	// The engine's other light is the key: a directional light, the sun, that
	// lights everything at once. A point light is a lamp - it lights what is
	// near it and falls off with distance, which is what colours a scene
	// from inside it.
	struct PointLight
	{
		glm::vec3 Position = glm::vec3(0.0f);
		glm::vec3 Color = glm::vec3(1.0f);
		float Intensity = 1.0f;

		// The falloff, the classic point-light attenuation:
		// 1 / (constant + linear * d + quadratic * d^2).
		float Constant = 1.0f;
		float Linear = 0.09f;
		float Quadratic = 0.032f;
	};

	// How many point lights a frame is drawn with.
	//
	// The count is fixed because the lights travel in the frame's uniform
	// buffer: the shader reads a fixed-size array, so the engine has to
	// decide which lights make the show and which do not.
	inline constexpr uint32_t kMaxPointLights = 8;

	// The lights one frame is drawn with.
	struct PointLightSet
	{
		std::array<PointLight, kMaxPointLights> Lights{};
		uint32_t Count = 0;
	};

	// The scene's point lights, in the order the scene holds them.
	//
	// The key light - the directional one - is not among them: it lights
	// everything at once, and it is the one the shadow map is built from.
	std::vector<PointLight> CollectPointLights(Scene& scene);

	// Picks the lights a frame is drawn with.
	//
	// A scene may hold more lights than the shader can take, so the ones
	// that matter are kept: the brightest first. A scene with room for them
	// all keeps the order it was given - the sort is stable - so the lights
	// are drawn in the order the author wrote them.
	PointLightSet SelectPointLights(const std::vector<PointLight>& candidates);

}  // namespace Forge
