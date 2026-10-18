#pragma once

#include <glm/glm.hpp>

#include <utility>
#include <vector>

namespace Forge {

	// A shadow bias in NDC depth, for the `proj.z - bias > depth` test the
	// shader makes against the map.
	//
	// A constant bias cannot do this job: it has to be large enough to cover a
	// surface that meets the light at a grazing angle, and at that size it
	// detaches every shadow from the object that casts it (the classic
	// peter-panning). The two standard cures are both here - a normal offset,
	// which moves the sample off the surface it is sampling against by an
	// amount that scales with the texel's own world size, and a depth bias
	// scaled by the same slope.
	//
	// `texelWorld` is the width of one map texel in world units; `texelDepth`
	// is the depth one NDC step covers. A texel that is large in the world is a
	// coarse map, and needs a proportionally larger offset.
	struct ShadowBias
	{
		float NormalOffsetWorld = 0.0f;
		float DepthBiasNdc = 0.0f;
	};

	ShadowBias ComputeShadowBias(float texelWorld, float texelDepth,
		float constantWorldBias = 0.02f, float slopeScale = 2.0f, float maxNormalOffset = 4.0f);

	// The orthographic shadow camera for a directional light: the map is fitted
	// to the scene's bounds, which is the only way a fixed box can be both
	// useful and safe. A box that is too small leaves geometry outside it, and
	// outside the map the shadow lookup has no answer - the fragment is either
	// lit by an edge texel it never touched, or reported as outside the map, so
	// shading appears at arbitrary places in the scene and moves as the camera
	// moves. A box far too large spends the whole map on empty space.
	struct ShadowCamera
	{
		glm::mat4 ViewProjection = glm::mat4(1.0f);
		float NearPlane = 0.0f;
		float FarPlane = 0.0f;

		// The texel dimensions follow from the volume the camera frames and the
		// map's resolution, and the bias depends on them: a map whose texels are
		// 30 centimetres wide cannot be used with a bias tuned for millimetre
		// texels, and the shader cannot work out the volume on its own.
		float TexelWorld = 0.0f;
		float TexelDepth = 0.0f;

		// False means "there is no shadow camera this frame": the caller must
		// leave the shadow map alone, not draw into it with the identity.
		bool Valid = false;
	};

	// `boxes` are the world-space AABBs of everything that casts and receives
	// shadow. `lightDirection` points *towards* the light - the same convention
	// the PBR shader's `u_LightDirection` uses, so the shadow falls the way the
	// light lights.
	//
	// An invalid camera is returned for a scene with nothing to frame and for a
	// direction that is not a direction. Identity is not a shadow transform: an
	// identity view-projection projects geometry by its own world position, so
	// the map holds a picture of the scene from nowhere and every fragment
	// compares its world position against texels that describe something else.
	ShadowCamera BuildShadowCamera(const std::vector<std::pair<glm::vec3, glm::vec3>>& boxes,
		const glm::vec3& lightDirection, float mapResolution = 2048.0f);

}
