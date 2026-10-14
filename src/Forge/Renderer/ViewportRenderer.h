#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Renderer/Texture.h"

namespace Forge {

	class Scene;

	class ViewportRenderer
	{
	public:
		ViewportRenderer();
		~ViewportRenderer();

		void Init(uint32_t width, uint32_t height);
		void Shutdown();
		void Resize(uint32_t width, uint32_t height);

		void Render(Scene* scene, const glm::mat4& viewProjectionMatrix);

		Ref<Texture> GetRenderTexture() const { return m_RenderTexture; }
		uint32_t GetWidth() const { return m_Width; }
		uint32_t GetHeight() const { return m_Height; }

	private:
		void CreateRenderTargets();
		void DestroyRenderTargets();

		Ref<Texture> m_RenderTexture;
		uint32_t m_Width;
		uint32_t m_Height;
	};

}
