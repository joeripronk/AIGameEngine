#pragma once

#include "Forge/Renderer/RenderCommand.h"
#include "Forge/Renderer/Shader.h"
#include "Forge/Renderer/Shaders.h"
#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <glm/glm.hpp>

namespace Forge {

	class Renderer
	{
	public:
		static void Init();
		static void Shutdown();

		static void OnWindowResize(uint32_t width, uint32_t height);

		static void BeginScene();
		static void EndScene();

		// Records any pending CPU->GPU buffer uploads for a vertex array. Must
		// be called between BeginScene() and EndScene() (i.e. while the frame's
		// command list is open).
		static void FlushPendingUploads(const Ref<class VertexArray>& vertexArray);

		static void Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));
		static void Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform, const Ref<class Texture>& albedoTexture);

		static void BeginShadowPass();
		static void EndShadowPass();

		static void BeginHDRPass();
		static void EndHDRPass();

		static void BeginSSAOPass();
		static void EndSSAOPass();

		static void BeginFinalPass();
		static void EndFinalPass();

		static void SubmitFinal();

		static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
		static ShaderLibrary& GetShaderLibrary() { return s_ShaderLibrary; }

	private:
		struct SceneData
		{
			glm::mat4 ViewProjectionMatrix;
			glm::mat4 ShadowViewProjection;
		};

		static Scope<SceneData> s_SceneData;
		static ShaderLibrary s_ShaderLibrary;
		static uint32_t s_Width;
		static uint32_t s_Height;
		static nvrhi::CommandListHandle s_CommandList;

		// Fullscreen triangle-pair used by the post-processing passes. Retained so
		// its GPU buffers stay alive and can be uploaded on the first frame.
		static Ref<class VertexArray> s_FullscreenQuad;
	};

}
