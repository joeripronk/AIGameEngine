#include "fgpch.h"

#include "Forge/Renderer/PointLights.h"

#include "Forge/Scene/Components.h"
#include "Forge/Scene/Scene.h"

#include <algorithm>
#include <numeric>

namespace Forge {

	std::vector<PointLight> CollectPointLights(Scene& scene)
	{
		std::vector<PointLight> lights;

		// The view asks for both components, so a light with no place in the
		// world is not in it: a lamp that cannot say where it is cannot
		// light anything.
		auto view = scene.GetAllEntitiesWith<LightComponent, TransformComponent>();
		for (auto handle : view)
		{
			const auto& light = view.get<LightComponent>(handle);
			if (light.Type != LightComponent::Type::Point)
				continue;

			const auto& transform = view.get<TransformComponent>(handle);

			PointLight point;
			point.Position = transform.Translation;
			point.Color = light.Color;
			point.Intensity = light.Intensity;
			point.Constant = light.Constant;
			point.Linear = light.Linear;
			point.Quadratic = light.Quadratic;
			lights.push_back(point);
		}

		return lights;
	}

	PointLightSet SelectPointLights(const std::vector<PointLight>& candidates)
	{
		PointLightSet selected;

		// The brightest first, and the order of the rest kept: a scene with
		// room for every light is drawn in the order the author wrote them,
		// and a scene with more lights than the shader can take keeps the
		// ones that are doing the most.
		//
		// The sort is stable on purpose: two lights of the same brightness
		// must not swap places from frame to frame, or the scene flickers
		// between two arrangements of the same show.
		std::vector<uint32_t> order(candidates.size());
		std::iota(order.begin(), order.end(), 0U);
		std::stable_sort(order.begin(), order.end(),
			[&candidates](uint32_t a, uint32_t b)
			{
				return candidates[a].Intensity > candidates[b].Intensity;
			});

		for (uint32_t index : order)
		{
			if (selected.Count >= kMaxPointLights)
				break;

			selected.Lights[selected.Count] = candidates[index];
			++selected.Count;
		}

		return selected;
	}

}  // namespace Forge
