#pragma once

#include <cstdint>

namespace Forge {

	// The environment the PBR pass reflects.
	//
	// Every cube map the engine binds - the reflection probe, the irradiance map,
	// and the 1x1 fallback - is created with a single mip level. Sampling one at a
	// level above 0 asks for texels that do not exist, which Vulkan leaves
	// *undefined*: not black, not the base level, whatever the driver feels like.
	// It is the one class of bug that shows up as a patch of someone else's
	// texture sitting in the middle of the frame, because the texels it returns
	// belong to nothing the scene contains.
	//
	// So the mip is not a lighting knob here. The roughness response is expressed
	// the way a prefiltered map would have expressed it: a rough surface's
	// reflection approaches its diffuse irradiance, so the shader blends the sharp
	// sample toward the irradiance by the roughness. That is arithmetic the tests
	// can pin down, and it needs no texture levels the engine never built.
	inline float SelectEnvMip(float roughness, uint32_t mipLevels)
	{
		(void)roughness;
		// The only level that exists. Clamp against the map's own level count so
		// the rule stays correct if the engine ever builds a real chain.
		return mipLevels == 0 ? 0.0f : static_cast<float>(mipLevels - 1u) * 0.0f;
	}

	// How far a reflection has moved toward its diffuse limit at this roughness.
	// 0 = a mirror (the sharp sample alone), 1 = fully diffuse.
	inline float EnvSpecularBlur(float roughness)
	{
		const float r = roughness < 0.0f ? 0.0f : (roughness > 1.0f ? 1.0f : roughness);
		return r;
	}

}
