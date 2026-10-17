#pragma once

#include "Forge/Core/Base.h"

#include <glm/glm.hpp>

namespace Forge {

	class RendererAPI
	{
	public:
		enum class API
		{
			None = 0, Vulkan = 1
		};

	public:
		virtual ~RendererAPI() = default;

		virtual void Init() = 0;
		virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
		virtual void SetClearColor(const glm::vec4& color) = 0;
		virtual void Clear() = 0;

		virtual void DrawIndexed(const Ref<class VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;

		virtual void SetLineWidth(float width) = 0;

		// Creates the backend's presentation surface and size-dependent render
		// targets for the given native window. Called once during renderer
		// startup; backends that cannot present may leave it unimplemented.
		virtual void SetupSwapchain(void* /*windowHandle*/, uint32_t /*width*/, uint32_t /*height*/) {}

		// Rebuilds the backend's size-dependent render targets after the window
		// changed size. Called on every resize, before the next frame is
		// recorded; a backend with no size-dependent targets needs no override.
		virtual void ResizeRenderTargets(uint32_t /*width*/, uint32_t /*height*/) {}

		// Releases backend resources. Called from the renderer's shutdown path.
		virtual void Shutdown() {}

		static API GetAPI() { return s_API; }
		static Scope<RendererAPI> Create();
	private:
		static API s_API;
	};

}
