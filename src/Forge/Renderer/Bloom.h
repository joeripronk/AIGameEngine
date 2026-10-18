#pragma once

// The bloom post-processing parameters and kernels, following the chain
// Cherno presents publicly (luminance → 13-tap downsample → repeated
// 16-tap golden-spiral blur → composite with the tonemap). The public Hazel
// repository contains no bloom code - this follows the documented pattern,
// not a verbatim port, and TODO.md says so.
//
// The maths lives here, in ForgeCore, where it is unit-testable with no
// graphics device: the soft-threshold filter is pure arithmetic and the
// tap kernels are constants. bloom.frag and blur.frag evaluate the same
// formulas in GLSL; these functions are the definition those shaders
// implement, and BloomTests asserts the properties that must hold
// (a filter that passes, kernels that stay in the unit square and cover it
// evenly), so a shader transcription error has a test that fails.

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>
#include <vector>

namespace Forge {

	struct BloomParams
	{
		// Composite weight added to the HDR radiance before tonemapping.
		// 0 disables the effect entirely. Bloom is added in the tonemap
		// pass, not after it: glow is light, and tonemapping the sum keeps
		// the highlight rolloff of the whole frame consistent.
		float Intensity = 0.05f;

		// Only luminance above this contributes. 1.0 means "highlights
		// only", which is the Hazel look: lamps glow, walls do not.
		float Threshold = 1.0f;

		// How much of the transition below/around the threshold is soft.
		// 0 is a hard cutoff, which strobes on moving geometry as luminance
		// crosses the threshold frame to frame; 0.5 is the published default.
		float Knee = 0.5f;

		// Extra gain on the filtered luminance.
		float Scale = 1.0f;
	};

	// The golden ratio, the spiral's angular step base. Same constant in
	// blur.frag - one number, two languages.
	inline constexpr float kGoldenRatio = 1.6180339887498948482f;

	// Luminance -> bloom weight for the luminance filter. Pure function of
	// the parameters, matching bloom.frag:
	//
	//   x = max(luma - T, 0)
	//   soft = x - x*x / (x + T*knee)
	//   weight = clamp(soft, 0, T*knee) / max(luma, eps) * scale
	//
	// The division by luma normalizes the filtered energy, which is what
	// stops the bright texels of a texture crawling over each other after
	// the blurs (the filter picks out the luminance *excess*, the division
	// turns it back into a per-texel weight).
	inline float BloomLumaWeight(float luminance, const BloomParams& params)
	{
		if (luminance <= 0.0f)
			return 0.0f;

		const float x = luminance - params.Threshold > 0.0f ? luminance - params.Threshold : 0.0f;
		const float knee = params.Threshold * params.Knee;
		const float soft = x - x * x / (x + knee + 1e-5f);
		const float softAmount = soft < 0.0f ? 0.0f : (soft < knee ? soft : knee);
		return softAmount / luminance * params.Scale;
	}

	namespace BloomKernel {

		// The 13-tap downsample offsets (the Kawase-style box from the
		// published chain): texel centers of a 3x3 grid plus the four
		// corner-adjacent taps, in units of the destination texel. Applied
		// against the full-resolution source, the 4 texels each destination
		// texel covers are all represented.
		inline std::vector<glm::vec2> DownsampleOffsets()
		{
			static const std::vector<glm::vec2> offsets = {
				glm::vec2(-1.0f, -1.0f), glm::vec2(0.0f, -1.0f), glm::vec2(1.0f, -1.0f),
				glm::vec2(-1.0f, 0.0f),  glm::vec2(0.0f, 0.0f),  glm::vec2(1.0f, 0.0f),
				glm::vec2(-1.0f, 1.0f),  glm::vec2(0.0f, 1.0f),  glm::vec2(1.0f, 1.0f),
				glm::vec2(-1.5f, -1.5f), glm::vec2(1.5f, -1.5f),
				glm::vec2(-1.5f, 1.5f),  glm::vec2(1.5f, 1.5f),
			};
			return offsets;
		}

		// The 16-tap golden-spiral blur offsets (the Poisson-like disc from
		// the published chain): tap i sits at radius sqrt((i+0.5)/N) and
		// angle 2*pi*i/phi, so the taps fill the disc evenly without the
		// ring artifacts a uniform ring of taps produces. Unit-scaled: the
		// pass multiplies by texel size and a per-iteration radius.
		inline std::vector<glm::vec2> BlurOffsets()
		{
			static const std::vector<glm::vec2> offsets = []()
			{
				std::vector<glm::vec2> taps;
				taps.reserve(16);
				const float goldenAngle = 2.0f * 3.14159265358979323846f / kGoldenRatio;
				for (int i = 0; i < 16; ++i)
				{
					const float radius = std::sqrt((static_cast<float>(i) + 0.5f) / 16.0f);
					const float angle = goldenAngle * static_cast<float>(i);
					taps.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
				}
				return taps;
			}();
			return offsets;
		}

	}  // namespace BloomKernel

}  // namespace Forge
