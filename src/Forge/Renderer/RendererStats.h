#pragma once

#include <cstdint>

namespace Forge {

	// Per-frame renderer counters, the pattern Hazel exposes as
	// Renderer2D::Statistics: "the engine drew N things" has to be readable
	// while the editor runs, because a frame that draws 10x what the scene
	// contains (no culling, no upload caching) looks exactly like a frame that
	// draws the right amount.
	//
	// The counters are pure arithmetic in the core so unit tests can assert on
	// them; the Vulkan renderer fills them from the real draw loop.
	struct RendererStatistics
	{
		uint32_t DrawCalls3D = 0;
		uint32_t TrianglesSubmitted = 0;
		uint32_t TexturesBound = 0;
		uint32_t FrustumCulled = 0;

		void Reset()
		{
			DrawCalls3D = 0;
			TrianglesSubmitted = 0;
			TexturesBound = 0;
			FrustumCulled = 0;
		}

		void AddDrawCall(uint32_t triangles, bool withTexture)
		{
			++DrawCalls3D;
			TrianglesSubmitted += triangles;
			if (withTexture)
				++TexturesBound;
		}

		void AddFrustumCulled(uint32_t count = 1)
		{
			FrustumCulled += count;
		}

		uint32_t GetTotalVertexCount() const { return TrianglesSubmitted * 3; }
	};

}
