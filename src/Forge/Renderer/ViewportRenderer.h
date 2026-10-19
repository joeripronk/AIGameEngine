#pragma once

#include "Forge/Core/Base.h"

namespace Forge {

	class Scene;

	// Draws the scene from the editor camera into the renderer's HDR target.
	// The panel displays the composited frame through Renderer::
	// GetCompositedTextureView(); this class owns no GPU resources (Phase 5:
	// the renderer owns the targets, and a second set would be a second
	// pipeline contract to keep in sync).
	class ViewportRenderer
	{
	public:
		ViewportRenderer();
		~ViewportRenderer();

		void Init(uint32_t width, uint32_t height);
		void Shutdown();
		void Resize(uint32_t width, uint32_t height);

		void Render(Scene* scene, const glm::mat4& viewProjectionMatrix);

		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

	private:
		uint32_t m_Width;
		uint32_t m_Height;
	};

}
