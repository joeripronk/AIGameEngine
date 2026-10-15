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

		// Presentation. AcquireSwapchainImage() must be called before BeginScene()
		// and PresentSwapchainImage() after EndScene(). Acquire returns
		// UINT32_MAX when presentation is unavailable, in which case rendering
		// proceeds without a swapchain and Present is a no-op.
		static uint32_t AcquireSwapchainImage();
		static void PresentSwapchainImage(uint32_t imageIndex);
		static bool HasSwapchain();

		// The GLFW window the renderer presents to, used to create the surface
		// and swapchain. Owned by Application.
		static void SetActiveWindowHandle(void* windowHandle) { s_WindowHandle = windowHandle; }
		static void* GetActiveWindowHandle() { return s_WindowHandle; }

		static void Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform = glm::mat4(1.0f));
		static void Submit(const Ref<class Shader>& shader, const Ref<class VertexArray>& vertexArray, const glm::mat4& transform, const Ref<class Texture>& albedoTexture);

		// Draws every entity in `scene` that has a MeshComponent and a primary
		// camera, using the PBR shader, into the given swapchain framebuffer.
		// Must be called between BeginScene() and EndScene().
		static void RenderScene(class Scene* scene, uint32_t swapchainImage);

		// Renders every entity with a MeshComponent to the shadow map.
		// Must be called between BeginShadowPass() and EndShadowPass().
		static void RenderShadowScene(class Scene* scene);

		// Post-processing passes: SSAO, HDR tonemapping, final composite.
		// These must be called between BeginScene() and EndScene(), after RenderScene().
		static void BeginSSAOPass();
		static void EndSSAOPass();
		static void BeginHDRPass();
		static void EndHDRPass();
		static void BeginFinalPass();
		static void EndFinalPass();
		static void SubmitFinal();

		// Reads the given swapchain image back to the CPU and writes it as a
		// binary PPM. Used to verify rendering without a windowing system.
		static bool CaptureSwapchainImage(uint32_t imageIndex, const std::string& path);

		static void BeginShadowPass(class Scene* scene);
		static void EndShadowPass();

		static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }
		static ShaderLibrary& GetShaderLibrary() { return s_ShaderLibrary; }
		static Ref<class PbrRenderer> GetPbrRenderer() { return s_PbrRenderer; }

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

		// Fallback cube drawn for mesh entities that carry no mesh of their own.
		// Retained for the same reason: it owns GPU vertex and index buffers,
		// which must outlive the frame that references them. Building it per
		// RenderScene call destroyed those buffers every frame while the GPU was
		// still reading them.
		static Ref<class Mesh> s_DefaultCube;

		// Set when a present reports the swapchain is out of date; the next
		// AcquireSwapchainImage() recreates it.
		static bool s_SwapchainNeedsRecreate;

		// GLFW window handle used to create the Vulkan surface and swapchain.
		static void* s_WindowHandle;

		// Draws the scene with the PBR shader into the swapchain.
		static Ref<class PbrRenderer> s_PbrRenderer;

		// Set once RenderScene has successfully resolved a camera.
		static bool s_RenderSceneVerified;

		// Shadow map framebuffer (depth-only).
		static nvrhi::FramebufferHandle s_ShadowFramebuffer;

		// Post-processing: framebuffer for the tonemapped buffer.
		static nvrhi::FramebufferHandle s_TonemappedFramebuffer;

		// Post-processing: framebuffer for the SSAO buffer.
		static nvrhi::FramebufferHandle s_SSAOFramebuffer;

		// The current swapchain image index (set by AcquireSwapchainImage).
		static uint32_t s_CurrentSwapchainImage;
	};

}
