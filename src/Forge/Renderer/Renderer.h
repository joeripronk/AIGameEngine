#pragma once

#include "Forge/Renderer/RenderCommand.h"
#include "Forge/Renderer/RendererStats.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Forge {

	class Texture;

	class Renderer
	{
	public:
		static void Init();
		static void Shutdown();

		static void OnWindowResize(uint32_t width, uint32_t height);
		// Opt into supersampled rendering. 1 disables it; >1 makes the offscreen
		// targets larger than the window so the Final pass can downsample them
		// back onto the swapchain image. Call before Renderer::Init() or while running.
		static void SetRenderScale(float scale);
		static float GetRenderScale();

		// Opt into real-time scene reflections. The scene is redrawn from the
		// probe position into a cubemap each frame and bound as the specular
		// environment, so polished metal mirrors its surroundings. Returns false
		// when the device is missing - the scene then renders unreflected
		// rather than failing.
		[[nodiscard]]
		static bool EnableReflectionProbe(uint32_t size = 256);
		static bool IsReflectionProbeEnabled();

		// Where the reflection probe samples the scene from. Defaults to the
		// origin; set it near the reflective geometry for believable mirrors.
		static void SetReflectionProbePosition(const glm::vec3& position);

		// The PBR albedo texture for a path, loaded once and shared by every
		// pass that draws with it. A failed path stays in the cache as null, so
		// the load failure is logged once per path rather than per frame, and
		// the material falls back to its albedo colour.
		static Ref<Texture> GetAlbedoTexture(const std::string& path);

		static void BeginScene();
		static void EndScene();

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

		// The window's size, used by Init() to size every render target. It has to
		// come from the window rather than a constant: the swapchain is clamped
		// to the surface extent, so guessing here leaves the offscreen targets a
		// different size to the swapchain and every post-processing pass sets a
		// viewport/scissor larger than the attachment it draws into.
		static void SetActiveWindowSize(uint32_t width, uint32_t height)
		{
			s_InitialWidth = width;
			s_InitialHeight = height;
		}

		// Draws every entity in `scene` that has a MeshComponent and a primary
		// camera, using the PBR shader, into the frame's HDR target.
		// Must be called between AcquireSwapchainImage() and EndScene().
		//
		// Entities whose world bounding box lies entirely outside the camera
		// frustum are culled and counted, and the per-frame counters below
		// describe exactly what was submitted.
		static void RenderScene(class Scene* scene, uint32_t swapchainImage);

		// Per-frame draw statistics, filled by RenderScene and read by the
		// editor's stats display (the Hazel statistics pattern).
		static RendererStatistics GetStatistics();

		// Post-processing passes: SSAO, bloom, HDR tonemapping, final composite.
		// These must be called between BeginScene() and EndScene(), after RenderScene().
		static void BeginSSAOPass();
		static void EndSSAOPass();
		static void BeginBloomPass();
		static void EndBloomPass();
		static void BeginHDRPass();
		static void EndHDRPass();
		static void BeginFinalPass();
		static void EndFinalPass();

		// Bloom composite weight, added to the HDR radiance before tonemapping.
		// 0 disables the effect; call after BeginBloomPass has run (the pass
		// records every frame, the weight decides what the tonemap adds).
		static void SetBloomIntensity(float intensity);
		static float GetBloomIntensity();

		// Reads the given swapchain image back to the CPU and writes it as a
		// binary PPM. Used to verify rendering without a windowing system.
		static bool CaptureSwapchainImage(uint32_t imageIndex, const std::string& path);

		// The shadow pass draws with the transform RenderScene computed for this
		// frame, so the map and the fragment-side lookup are one projection.
		static void BeginShadowPass(class Scene* scene, const glm::mat4& shadowViewProjection);
		static void EndShadowPass();

		static RendererAPI::API GetAPI() { return RendererAPI::GetAPI(); }

		// --- Vulkan handles for the ImGui overlay backend ---
		//
		// The vendor ImGui backend is initialised with the device the renderer
		// owns and draws into the frame's command buffer between BeginFinalPass
		// and EndScene.
		static void* GetVulkanInstanceHandle();
		static void* GetVulkanPhysicalDeviceHandle();
		static void* GetVulkanDeviceHandle();
		static void* GetVulkanGraphicsQueueHandle();
		static uint32_t GetVulkanGraphicsQueueIndex();
		static uint32_t GetSwapchainImageCount();
		static uint32_t GetSwapchainVkFormat();       // VkFormat, widened for the header
		static void* GetCurrentCommandBuffer();
		// VkRenderPass, widened for the header: the present pass the
		// overlay's pipeline has to be built for.
		static void* GetPresentRenderPass();

		// The tonemapped frame as a VkImageView in SHADER_READ_ONLY layout -
		// what the editor's viewport panel displays as an ImGui texture.
		// VK_NULL_HANDLE when there are no targets.
		static void* GetCompositedTextureView();

	private:
		// Rebuilds every size-dependent resource for a new frame size.
		static void ApplyFrameSize(uint32_t width, uint32_t height);

		// Set when the next AcquireSwapchainImage() must skip BeginFrame (the


		static uint32_t s_Width;
		static uint32_t s_Height;

		// Set when a present reports the swapchain is out of date; the next
		// AcquireSwapchainImage() recreates it.
		static bool s_SwapchainNeedsRecreate;

		// The window's size as the last resize event reported it, and the size
		// it had in the previous frame. A size that has not changed since the
		// last frame has settled, and a settled size is the one worth
		// rebuilding the swapchain and the render targets for.
		static uint32_t s_WindowWidth;
		static uint32_t s_WindowHeight;
		static uint32_t s_SettledWidth;
		static uint32_t s_SettledHeight;

		// GLFW window handle used to create the Vulkan surface and swapchain.
		static void* s_WindowHandle;

		// Window size reported by Application before Init() runs; 0 when unknown.
		static uint32_t s_InitialWidth;
		static uint32_t s_InitialHeight;

		static float s_RenderScale;
	};

}
