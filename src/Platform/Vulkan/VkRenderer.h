#pragma once

#include "Forge/Core/Base.h"

#include "Platform/Vulkan/Device/DebugName.h"
#include "Platform/Vulkan/Device/VkDevice.h"
#include "Platform/Vulkan/Device/VkInstance.h"
#include "Platform/Vulkan/Device/VkPhysicalDevice.h"
#include "Platform/Vulkan/Device/VkQueue.h"
#include "Platform/Vulkan/Frame/FrameLoop.h"
#include "Platform/Vulkan/Frame/Swapchain.h"
#include "Platform/Vulkan/Memory/Allocator.h"
#include "Platform/Vulkan/Memory/StagingPool.h"
#include "Platform/Vulkan/Memory/VmaDeviceAllocator.h"
#include "Platform/Vulkan/Pipeline/DescriptorPool.h"
#include "Platform/Vulkan/Pipeline/PipelineCache.h"
#include "Platform/Vulkan/Pipeline/ShaderModule.h"
#include "Platform/Vulkan/Pipeline/VertexLayout.h"

#include <vk_mem_alloc.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Forge {

	class Scene;
	class Mesh;
	class Texture;
	class CpuTexture;
	struct Vertex;

	namespace Vulkan {

		// The raw-Vulkan renderer (BRUTAL rewrite, Phase 5). It owns the whole
		// frame: the device (Instance/PhysicalDevice/Device/Queue via the Phase 1
		// layer), the swapchain and frame loop (Phase 3), the offscreen chain
		// (HDR + depth, SSAO, bloom half-res ping-pong, tonemapped, shadow map),
		// the seven passes in engine order (Shadow -> PBR/HDR-scene -> SSAO ->
		// Bloom -> Tonemap -> Final), staged mesh/texture uploads (Phase 2) and
		// the accounting descriptor pool (Phase 4).
		//
		// Same formulas, same uniforms, same shader contract as the nvrhi
		// renderer it replaces: the uniform blocks mirror resources/shaders
		// byte for byte, and the parameters (clear colour, exposure, bloom
		// defaults, SSAO params) are the values the engine renders with today.
		class VkRenderer final
		{
		public:
			static VkRenderer& Get();

			VkRenderer(const VkRenderer&) = delete;
			VkRenderer& operator=(const VkRenderer&) = delete;

			// --- Device and frame ---

			[[nodiscard]] bool InitDevice(void* glfwWindow, uint32_t width, uint32_t height);
			void ShutdownDevice();
			[[nodiscard]] bool IsReady() const { return m_DeviceReady; }

			// The full startup the Renderer facade drives: device, render
			// passes, samplers and fallbacks, the fullscreen quad, descriptor
			// layouts, pipelines and the size-dependent targets - in dependency
			// order, reporting the first failure.
			[[nodiscard]] bool Initialize(void* glfwWindow, uint32_t presentWidth, uint32_t presentHeight,
				uint32_t renderWidth, uint32_t renderHeight);

			// Presentation size (window) and offscreen size (presentation x
			// render scale). Recreating targets is the resize path.
			[[nodiscard]] bool CreateTargets(uint32_t presentWidth, uint32_t presentHeight, uint32_t renderWidth, uint32_t renderHeight);
			void DestroyTargets();
			[[nodiscard]] bool HasTargets() const { return m_HdrImage != VK_NULL_HANDLE; }

			// Frame loop: BeginFrame acquires the swapchain image, waits the
			// per-image fence, resets the command pool and begins the frame's
			// one-shot command buffer. UINT32_MAX means "no frame this time".
			[[nodiscard]] uint32_t BeginFrame();

			// Close the frame's render pass (whatever is open), end the
			// command buffer, submit and present.
			void EndScene();
			void SubmitFrameOnly();
			void EndFrame(uint32_t imageIndex);
			[[nodiscard]] ::VkCommandBuffer CurrentCommandBuffer() const;

			// The command buffer for the current frame (the Final pass target is
			// the swapchain image acquired by BeginFrame).
			[[nodiscard]] uint32_t CurrentImageIndex() const { return m_CurrentImageIndex; }

			// --- Scene rendering ---

			struct CameraParams
			{
				glm::mat4 ViewProjection = glm::mat4(1.0f);
				glm::mat4 ShadowViewProjection = glm::mat4(1.0f);
				glm::vec3 CameraPosition = glm::vec3(0.0f);
				glm::vec3 LightDirection = { -0.5f, -1.0f, -0.3f };
				glm::vec3 LightColor = { 1.0f, 1.0f, 1.0f };
				float LightIntensity = 1.0f;
				glm::vec3 AmbientLight = { 0.03f, 0.03f, 0.04f };
				glm::vec2 ShadowBias = glm::vec2(0.0f);
			};

			struct ObjectParams
			{
				glm::mat4 Model = glm::mat4(1.0f);
				glm::mat3 NormalMatrix = glm::mat3(1.0f);
				glm::vec3 Albedo = glm::vec3(1.0f);
				float Metallic = 0.0f;
				float Roughness = 0.5f;
				float AO = 1.0f;
				glm::vec3 Emissive = glm::vec3(0.0f);
				float EmissiveIntensity = 0.0f;
			};

			// Records the per-frame camera upload. The HDR target's clear lives
			// in the render pass load operations; draws follow with DrawObject.
			void BeginScene(const CameraParams& camera);

			// One PBR draw. Geometry is uploaded (and cached) on first use.
			// `probeDraw` binds the static environment fallback: a probe pass
			// must not sample the cubemap it is writing.
			void DrawObject(const Ref<Mesh>& mesh, const Ref<Texture>& albedoTexture,
				const ObjectParams& object, bool probeDraw = false);

			// The shadow pass: one camera upload (light view-projection) and one
			// depth-only draw per mesh.
			void BeginShadowScene(const glm::mat4& shadowViewProjection);
			void DrawShadowObject(const Ref<Mesh>& mesh, const glm::mat4& model);

			// --- Post passes (engine order: SSAO -> Bloom -> Tonemap -> Final) ---

			void RenderSSAOPass(const glm::mat4& viewProjection);
			void RenderBloomPass();
			void RenderTonemapPass();
			void RenderFinalPass();

			// --- Reflection probe ---

			[[nodiscard]] bool EnableReflectionProbe(uint32_t size);
			[[nodiscard]] bool IsReflectionProbeEnabled() const { return m_ProbeEnabled; }
			void SetReflectionProbePosition(const glm::vec3& position) { m_ProbePosition = position; }
			[[nodiscard]] const glm::vec3& GetReflectionProbePosition() const { return m_ProbePosition; }

			struct ProbeDraw
			{
				Ref<Mesh> MeshHandle;
				Ref<Texture> AlbedoTexture;
				glm::mat4 Model = glm::mat4(1.0f);
				glm::mat3 NormalMatrix = glm::mat3(1.0f);
				glm::vec3 Albedo = glm::vec3(1.0f);
				glm::vec3 Emissive = glm::vec3(0.0f);
				float Metallic = 0.0f;
				float Roughness = 1.0f;
				float AO = 1.0f;
				float EmissiveIntensity = 0.0f;
			};

			// Redraws the scene from the probe into the six faces of the
			// reflection cubemap; the main pass binds it as the specular
			// environment.
			void RenderReflectionProbe(const CameraParams& baseCamera,
				const std::vector<ProbeDraw>& draws);

			// --- Environment map (IBL) ---

			// Uploads the equirectangular map and prefilters it into the
			// irradiance and prefiltered cubemaps the PBR pass binds.
			[[nodiscard]] bool LoadEnvironmentMap(const Ref<CpuTexture>& envMap);
			[[nodiscard]] bool HasEnvironmentMap() const { return m_EnvImageView != VK_NULL_HANDLE; }

			// --- Presentation / utilities ---

			[[nodiscard]] bool CaptureSwapchainImage(uint32_t imageIndex, const std::string& path);

			// Diagnostic: copy an offscreen target (SHADER_READ layout) back
			// to the CPU as a PPM. Used to bisect the pass chain headlessly.
			[[nodiscard]] bool CaptureTargetImage(const ::VkImage image, uint32_t width, uint32_t height,
				bool isFloat, const std::string& path);

			// ImGui backend accessors (the same handles ImGuiLayer feeds the
			// vendor backend today).
			[[nodiscard]] ::VkInstance GetInstanceHandle() const { return m_Instance.Handle(); }
			[[nodiscard]] ::VkPhysicalDevice GetPhysicalDeviceHandle() const { return m_PhysicalDevicePtr ? m_PhysicalDevicePtr->Handle() : VK_NULL_HANDLE; }
			[[nodiscard]] ::VkDevice GetLogicalDeviceHandle() const { return m_Device.Handle(); }
			[[nodiscard]] ::VkQueue GetGraphicsQueueHandle() const { return m_GraphicsQueue ? m_GraphicsQueue->Handle() : VK_NULL_HANDLE; }
			[[nodiscard]] uint32_t GetGraphicsQueueIndex() const { return m_GraphicsFamily; }
			[[nodiscard]] uint32_t GetImageCount() const { return m_Swapchain.IsNull() ? 0 : static_cast<uint32_t>(m_Swapchain.GetImages().size()); }
			[[nodiscard]] ::VkFormat GetSwapchainFormat() const { return m_Swapchain.GetFormat().format; }
			[[nodiscard]] uint32_t GetPresentWidth() const { return m_PresentWidth; }
			[[nodiscard]] uint32_t GetPresentHeight() const { return m_PresentHeight; }
			[[nodiscard]] uint32_t GetRenderWidth() const { return m_RenderWidth; }
			[[nodiscard]] uint32_t GetRenderHeight() const { return m_RenderHeight; }
			[[nodiscard]] uint32_t GetShadowMapSizeValue() const { return m_ShadowMapSize; }

		// The render pass the ImGui overlay draws into. The overlay's
		// draws are recorded into the frame's command buffer while this
		// pass is still open, so the ImGui pipeline has to be the one
		// this pass was created with.
		[[nodiscard]] ::VkRenderPass GetPresentRenderPass() const { return m_RenderPassPresent; }

			// The tonemapped target as an ImGui-sampleable view: the editor's
			// viewport panel displays what the Final pass composites.
			[[nodiscard]] ::VkImageView GetTonemapView() const { return m_TonemapView; }
			[[nodiscard]] ::VkImage GetTonemapImage() const { return m_TonemapImage; }
			[[nodiscard]] ::VkImage GetHdrImage() const { return m_HdrImage; }
			[[nodiscard]] ::VkImage GetSsaoImage() const { return m_SsaoImage; }
			[[nodiscard]] ::VkImage GetBloomHalfImage() const { return m_BloomHalfImage; }
			[[nodiscard]] ::VkImage GetDepthImage() const { return m_DepthImage; }

			// Diagnostic: re-open the HDR pass with its clear and close it.
			void ClearHdrTarget();
			void DiagDrawSsaoIntoTonemap();
			void ClearHdrTargetRed();

			// Recreates the swapchain for a new window size (the resize path:
			// targets follow the extent the swapchain reports).
			[[nodiscard]] bool RecreateSwapchain(uint32_t width, uint32_t height);
			[[nodiscard]] bool SwapchainNeedsRebuild() const { return m_Swapchain.NeedsRebuild(); }
			[[nodiscard]] bool HasSwapchain() const { return !m_Swapchain.IsNull(); }

			[[nodiscard]] static float GetBloomIntensity();
			static void SetBloomIntensity(float intensity);

			static float s_BloomIntensity;

			void WaitForGpuIdle();

		private:
			VkRenderer() = default;
			~VkRenderer();

			// Device-side setup split (VkRendererDevice.cpp /
			// VkRendererResources.cpp / VkRendererScene.cpp).
			[[nodiscard]] bool CreateRenderPasses();
			void DestroyRenderPasses();
			[[nodiscard]] bool CreateSamplersAndFallbacks();
			void DestroySamplersAndFallbacks();
			[[nodiscard]] bool CreateFullscreenQuad();
			void DestroyFullscreenQuad();
			[[nodiscard]] bool CreateDescriptorLayouts();
			void DestroyDescriptorLayouts();
			[[nodiscard]] bool CreatePipelines();
			void DestroyPipelines();

			// Dump the driver's pipeline cache to its file so the next
			// session starts from what this one compiled. Reports false
			// when the dump could not be written.
			[[nodiscard]] bool SavePipelineCache() const;

			// Graphics pipeline builder (VkRendererDevice.cpp; composed from
			// the Phase 4 presets). Dynamic viewport/scissor, so one pipeline
			// serves every target size. The cache is the renderer's dumped
			// VkPipelineCache (VK_NULL_HANDLE is legal: no caching).
			[[nodiscard]] static ::VkPipeline CreateGraphicsPipeline(const Device& device,
				::VkShaderModule vertex, ::VkShaderModule fragment,
				const VertexLayout* layout, ::VkRenderPass renderPass, uint32_t subpassIndex,
				::VkPipelineLayout pipelineLayout,
				const VkPipelineRasterizationStateCreateInfo& rasterizer,
				const VkPipelineDepthStencilStateCreateInfo& depth,
				const VkPipelineColorBlendStateCreateInfo& blend,
				::VkPipelineCache pipelineCache = VK_NULL_HANDLE);
			[[nodiscard]] bool CreateSwapchainFramebuffers();
			void DestroySwapchainFramebuffers();

			// Upload helpers (staged, one-shot command buffers through the
			// staging pool).
			[[nodiscard]] bool UploadMesh(const Ref<Mesh>& mesh);
			[[nodiscard]] bool UploadTexture(const Ref<Texture>& texture);
			void DestroyUploadCaches();

			struct MeshGpu
			{
				::VkBuffer VertexBuffer = VK_NULL_HANDLE;
				::VkBuffer IndexBuffer = VK_NULL_HANDLE;
				VmaAllocation VertexAlloc = nullptr;
				VmaAllocation IndexAlloc = nullptr;
				uint32_t IndexCount = 0;
			};
			struct TextureGpu
			{
				::VkImage Image = VK_NULL_HANDLE;
				::VkImageView View = VK_NULL_HANDLE;
				VmaAllocation Alloc = nullptr;
				uint32_t Width = 0;
				uint32_t Height = 0;
			};
			[[nodiscard]] MeshGpu* GetMeshGpu(const Ref<Mesh>& mesh);
			[[nodiscard]] const TextureGpu* GetTextureGpu(const Ref<Texture>& texture);

			// One PBR descriptor set, built per draw from the frame pool.
			// The object UBO range points at this draw's slot in the ring.
			[[nodiscard]] ::VkDescriptorSet BuildPbrSet(::VkBuffer objectBuffer, size_t objectOffset,
				::VkImageView albedoView, bool probeDraw);
			[[nodiscard]] ::VkDescriptorSet BuildShadowSet(::VkBuffer objectBuffer, size_t objectOffset);

		// One fullscreen-pass set from the frame pool: binding order is
		// the layout's (views, samplers, UBOs as declared).
		//
		// `layouts` names the image layout each view is in, in the same
		// order as `views`. A descriptor's imageLayout must match the
		// layout the image is actually in: a colour target is
		// SHADER_READ_ONLY_OPTIMAL, a depth target is
		// DEPTH_STENCIL_READ_ONLY_OPTIMAL, and the validation layer
		// rejects the draw when the two disagree.
		[[nodiscard]] ::VkDescriptorSet BuildPostSet(const DescriptorSetLayout& layout,
			std::span<const ::VkImageView> views,
			std::span<const VkImageLayout> layouts,
			std::span<const ::VkSampler> samplers,
			std::span<const VkDescriptorBufferInfo> buffers);

			void WriteCameraUniforms(const CameraParams& camera);
			void WriteShadowParams(const CameraParams& camera);

			// The active render pass (the scene/shadow/post passes open and
			// close it in engine order; EndFrame closes whatever is left).
			void EndActiveRenderPass();

			[[nodiscard]] bool CreateProbeResources();
			[[nodiscard]] bool CreateIblResources(uint32_t width, uint32_t height);

			// Device layer (Phase 1/2/3).
			Instance m_Instance;
			PhysicalDevice* m_PhysicalDevicePtr = nullptr;
			PhysicalDevice* m_PresentPhysical = nullptr;  // alias, same object
			Device m_Device;
			Ref<Queue> m_GraphicsQueue;
			Ref<VmaDeviceAllocator> m_Allocator;
			Ref<StagingPool> m_Staging;
			Swapchain m_Swapchain;
			Ref<FrameLoop> m_FrameLoop;

			uint32_t m_GraphicsFamily = 0;
			uint32_t m_PresentFamily = 0;
			uint32_t m_TransferFamily = 0;
			::VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
			bool m_DeviceReady = false;

			uint32_t m_PresentWidth = 0;
			uint32_t m_PresentHeight = 0;
			uint32_t m_RenderWidth = 0;
			uint32_t m_RenderHeight = 0;
			uint32_t m_CurrentImageIndex = UINT32_MAX;
			uint32_t m_CurrentTargetWidth = 0;   // viewport for scene draws
			uint32_t m_CurrentTargetHeight = 0;
			bool m_RenderPassOpen = false;

			// Render passes (classic; the decision to stay classic is recorded
			// in BRUTALTODO.md).
			::VkRenderPass m_RenderPassHdr = VK_NULL_HANDLE;
			::VkRenderPass m_RenderPassShadow = VK_NULL_HANDLE;
			::VkRenderPass m_RenderPassColor16 = VK_NULL_HANDLE;   // bloom + tonemap targets
			::VkRenderPass m_RenderPassR8 = VK_NULL_HANDLE;        // SSAO
			::VkRenderPass m_RenderPassPresent = VK_NULL_HANDLE;   // final composite
			::VkRenderPass m_RenderPassCubeFace = VK_NULL_HANDLE;  // IBL cubemap faces

			// Offscreen chain.
			::VkImage m_HdrImage = VK_NULL_HANDLE;
			::VkImageView m_HdrView = VK_NULL_HANDLE;
			::VkImage m_DepthImage = VK_NULL_HANDLE;
			::VkImageView m_DepthView = VK_NULL_HANDLE;
			::VkImage m_SsaoImage = VK_NULL_HANDLE;
			::VkImageView m_SsaoView = VK_NULL_HANDLE;
			::VkImage m_TonemapImage = VK_NULL_HANDLE;
			::VkImageView m_TonemapView = VK_NULL_HANDLE;
			::VkImage m_BloomHalfImage = VK_NULL_HANDLE;
			::VkImageView m_BloomHalfView = VK_NULL_HANDLE;
			::VkImage m_BloomAImage = VK_NULL_HANDLE;
			::VkImageView m_BloomAView = VK_NULL_HANDLE;
			::VkImage m_BloomBImage = VK_NULL_HANDLE;
			::VkImageView m_BloomBView = VK_NULL_HANDLE;

			VmaAllocation m_HdrAlloc = nullptr;
			VmaAllocation m_DepthAlloc = nullptr;
			VmaAllocation m_SsaoAlloc = nullptr;
			VmaAllocation m_TonemapAlloc = nullptr;
			VmaAllocation m_BloomHalfAlloc = nullptr;
			VmaAllocation m_BloomAAlloc = nullptr;
			VmaAllocation m_BloomBAlloc = nullptr;

			::VkFramebuffer m_FbHdr = VK_NULL_HANDLE;
			::VkFramebuffer m_FbSsao = VK_NULL_HANDLE;
			::VkFramebuffer m_FbTonemap = VK_NULL_HANDLE;
			::VkFramebuffer m_FbBloomHalf = VK_NULL_HANDLE;
			::VkFramebuffer m_FbBloomA = VK_NULL_HANDLE;
			::VkFramebuffer m_FbBloomB = VK_NULL_HANDLE;

			// Shadow map.
			uint32_t m_ShadowMapSize = 2048;
			::VkImage m_ShadowImage = VK_NULL_HANDLE;
			::VkImageView m_ShadowView = VK_NULL_HANDLE;
			VmaAllocation m_ShadowAlloc = nullptr;
			::VkFramebuffer m_FbShadow = VK_NULL_HANDLE;

			// Swapchain framebuffers (Final pass targets).
			std::vector<::VkFramebuffer> m_SwapchainFramebuffers;
			std::vector<::VkImageView> m_SwapchainViews;

			// Samplers.
			::VkSampler m_SamplerAlbedo = VK_NULL_HANDLE;
			::VkSampler m_SamplerShadow = VK_NULL_HANDLE;
			::VkSampler m_SamplerLinear = VK_NULL_HANDLE;
			::VkSampler m_SamplerBorderBlack = VK_NULL_HANDLE;
			::VkSampler m_SamplerBorderTransparent = VK_NULL_HANDLE;
			::VkSampler m_SamplerBorderWhite = VK_NULL_HANDLE;

			// Fallbacks: 1x1 textures so every descriptor is always bound.
			::VkImage m_WhiteImage = VK_NULL_HANDLE;
			::VkImageView m_WhiteView = VK_NULL_HANDLE;
			VmaAllocation m_WhiteAlloc = nullptr;
			::VkImage m_EnvFallbackImage = VK_NULL_HANDLE;   // 1x1 RGBA16F cube
			::VkImageView m_EnvFallbackView = VK_NULL_HANDLE;
			VmaAllocation m_EnvFallbackAlloc = nullptr;
			::VkImage m_IrradianceFallbackImage = VK_NULL_HANDLE;
			::VkImageView m_IrradianceFallbackView = VK_NULL_HANDLE;
			VmaAllocation m_IrradianceFallbackAlloc = nullptr;

			// Fullscreen quad.
			::VkBuffer m_QuadVertexBuffer = VK_NULL_HANDLE;
			VmaAllocation m_QuadVertexAlloc = nullptr;
			::VkBuffer m_QuadIndexBuffer = VK_NULL_HANDLE;
			VmaAllocation m_QuadIndexAlloc = nullptr;

			// Uniform buffers (host-visible rings).
			::VkBuffer m_CameraUbo = VK_NULL_HANDLE;
			VmaAllocation m_CameraUboAlloc = nullptr;
			void* m_CameraUboData = nullptr;
			::VkBuffer m_ShadowParamsUbo = VK_NULL_HANDLE;
			VmaAllocation m_ShadowParamsUboAlloc = nullptr;
			void* m_ShadowParamsUboData = nullptr;
			::VkBuffer m_ShadowCameraUbo = VK_NULL_HANDLE;
			VmaAllocation m_ShadowCameraUboAlloc = nullptr;
			void* m_ShadowCameraUboData = nullptr;
			::VkBuffer m_SsaoUbo = VK_NULL_HANDLE;
			VmaAllocation m_SsaoUboAlloc = nullptr;
			void* m_SsaoUboData = nullptr;
			::VkBuffer m_BloomUbo = VK_NULL_HANDLE;
			VmaAllocation m_BloomUboAlloc = nullptr;
			void* m_BloomUboData = nullptr;
			::VkBuffer m_BlurUbo = VK_NULL_HANDLE;
			VmaAllocation m_BlurUboAlloc = nullptr;
			void* m_BlurUboData = nullptr;
			::VkBuffer m_HdrUbo = VK_NULL_HANDLE;
			VmaAllocation m_HdrUboAlloc = nullptr;
			void* m_HdrUboData = nullptr;
			::VkBuffer m_FinalUbo = VK_NULL_HANDLE;
			VmaAllocation m_FinalUboAlloc = nullptr;
			void* m_FinalUboData = nullptr;
			::VkBuffer m_IrradianceUbo = VK_NULL_HANDLE;
			VmaAllocation m_IrradianceUboAlloc = nullptr;
			void* m_IrradianceUboData = nullptr;
			::VkBuffer m_PrefilterUbo = VK_NULL_HANDLE;
			VmaAllocation m_PrefilterUboAlloc = nullptr;
			void* m_PrefilterUboData = nullptr;

			// Per-object uniform ring (host-visible, one slot per draw).
			//
			// The slot stride is padded from the 176-byte payload to 256: a
			// descriptor's buffer offset must be a multiple of
			// minUniformBufferOffsetAlignment, and Mesa's is 256. 176-byte slots
			// are an invalid offset from slot 2 onward - the write is refused
			// and the shader reads garbage matrices, which is every draw
			// discarded.
			//
			// The ring is split into one region per swapchain image, and a
			// frame writes only inside the region of the image it draws: the
			// frame that last used that region is the one whose fence
			// BeginFrame waited. A ring that wraps across frames overwrites
			// the object data of frames still in flight - with the probe
			// redrawing the scene (6 + 1 + 1 draws per mesh) a 128-mesh frame
			// needs 1024 slots, and 256 shared slots wrapped onto submitted
			// frames every single frame.
			static constexpr size_t ObjectSlotBytes = 256;
			static constexpr uint32_t PoolRingSize = 8;   // >= swapchain images
			static constexpr uint32_t ObjectSlotsPerFrame = 1024;
			static constexpr uint32_t ObjectRingSize = PoolRingSize * ObjectSlotsPerFrame;
			::VkBuffer m_ObjectUbo = VK_NULL_HANDLE;
			VmaAllocation m_ObjectUboAlloc = nullptr;
			void* m_ObjectUboData = nullptr;
			uint32_t m_ObjectSlot = 0;

			// Shader modules.
			ShaderModule m_VertexShaderPbr;
			ShaderModule m_FragmentShaderPbr;
			ShaderModule m_VertexShaderShadow;
			ShaderModule m_FragmentShaderShadow;
			ShaderModule m_VertexShaderFullscreen;
			ShaderModule m_FragmentShaderSsao;
			ShaderModule m_FragmentShaderBloom;
			ShaderModule m_FragmentShaderBlur;
			ShaderModule m_FragmentShaderHdr;
			ShaderModule m_FragmentShaderFinal;
			ShaderModule m_FragmentShaderIrradiance;
			ShaderModule m_FragmentShaderPrefilter;

			// Pipelines.
			PipelineCache m_PipelineCache;   // dumped/loaded across sessions
			::VkPipeline m_PipelinePbr = VK_NULL_HANDLE;
			::VkPipeline m_PipelineShadow = VK_NULL_HANDLE;
			::VkPipeline m_PipelineSsao = VK_NULL_HANDLE;
			::VkPipeline m_PipelineBloom = VK_NULL_HANDLE;
			::VkPipeline m_PipelineBlur = VK_NULL_HANDLE;
			::VkPipeline m_PipelineTonemap = VK_NULL_HANDLE;
			::VkPipeline m_PipelineFinal = VK_NULL_HANDLE;
			::VkPipeline m_PipelineIrradiance = VK_NULL_HANDLE;
			::VkPipeline m_PipelinePrefilter = VK_NULL_HANDLE;
			std::vector<::VkPipeline> m_PipelineProbeFaces;   // irradiance/prefilter per face

			// Pipeline layouts.
			::VkPipelineLayout m_LayoutPbr = VK_NULL_HANDLE;
			::VkPipelineLayout m_LayoutShadow = VK_NULL_HANDLE;
			::VkPipelineLayout m_LayoutSsao = VK_NULL_HANDLE;
			::VkPipelineLayout m_LayoutBloom = VK_NULL_HANDLE;
			::VkPipelineLayout m_LayoutTonemap = VK_NULL_HANDLE;
			::VkPipelineLayout m_LayoutFinal = VK_NULL_HANDLE;
			::VkPipelineLayout m_LayoutIrradiance = VK_NULL_HANDLE;
			::VkPipelineLayout m_LayoutPrefilter = VK_NULL_HANDLE;

			// Descriptor layouts + a pool per swapchain image (Phase 4 accounting).
			//
			// A pool reset frees its sets, so it may only run once the frame
			// that allocated them has finished on the GPU. BeginFrame has just
			// waited the fence of the image it acquired - the frame that drew
			// that image imageCount frames ago - so the pool *that frame* used
			// is the safe one to reset and reuse. Indexing the ring by anything
			// other than the image index shifts the reset one frame early: the
			// sets it destroys are still in flight, and the GPU reading them
			// faults the context (VK_ERROR_DEVICE_LOST, measured on Intel with
			// the probe + shadow chain keeping the GPU several frames behind).
			// One pool is one frame in flight waiting to be corrupted.
			[[nodiscard]] DescriptorPool& CurrentDescriptorPool() { return m_DescriptorPools[m_CurrentPoolIndex]; }
			DescriptorSetLayout m_SetLayoutPbr;
			DescriptorSetLayout m_SetLayoutShadow;
			DescriptorSetLayout m_SetLayoutSsao;
			DescriptorSetLayout m_SetLayoutBloom;
			DescriptorSetLayout m_SetLayoutTonemap;
			DescriptorSetLayout m_SetLayoutFinal;
			DescriptorSetLayout m_SetLayoutIrradiance;
			DescriptorSetLayout m_SetLayoutPrefilter;
			std::array<DescriptorPool, PoolRingSize> m_DescriptorPools;
			uint32_t m_CurrentPoolIndex = UINT32_MAX;   // == image index while a frame is begun

			// Vertex layouts (Phase 4, from the CPU struct).
			VertexLayout m_VertexLayoutMesh;
			VertexLayout m_VertexLayoutQuad;

			// Environment / probe state.
			bool m_ProbeEnabled = false;
			uint32_t m_ProbeSize = 256;
			glm::vec3 m_ProbePosition = glm::vec3(0.0f);
			::VkImage m_ReflectionImage = VK_NULL_HANDLE;      // renderable cube
			::VkImageView m_ReflectionView = VK_NULL_HANDLE;
			VmaAllocation m_ReflectionAlloc = nullptr;
			::VkImage m_ReflectionDepthImage = VK_NULL_HANDLE;  // shared by the six faces
			::VkImageView m_ReflectionDepthView = VK_NULL_HANDLE;
			VmaAllocation m_ReflectionDepthAlloc = nullptr;
			std::vector<::VkImageView> m_ReflectionFaceViews;
			std::vector<::VkFramebuffer> m_ReflectionFaceFramebuffers;

			::VkImage m_EnvImage = VK_NULL_HANDLE;            // equirect 2D source
			::VkImageView m_EnvImageView = VK_NULL_HANDLE;
			VmaAllocation m_EnvAlloc = nullptr;
			uint32_t m_EnvWidth = 0;
			uint32_t m_EnvHeight = 0;
			::VkImage m_IrradianceImage = VK_NULL_HANDLE;
			::VkImageView m_IrradianceView = VK_NULL_HANDLE;
			VmaAllocation m_IrradianceAlloc = nullptr;
			::VkImage m_PrefilterImage = VK_NULL_HANDLE;
			::VkImageView m_PrefilterView = VK_NULL_HANDLE;
			VmaAllocation m_PrefilterAlloc = nullptr;
			std::vector<::VkFramebuffer> m_IrradianceFaceFramebuffers;
			std::vector<::VkImageView> m_IrradianceFaceViews;
			std::vector<::VkFramebuffer> m_PrefilterFaceFramebuffers;
			std::vector<::VkImageView> m_PrefilterFaceViews;   // per mip, per face
			uint32_t m_IrradianceSize = 64;
			uint32_t m_PrefilterSize = 128;
			uint32_t m_PrefilterMipCount = 1;

			// Frame state.
			bool m_SceneBegun = false;
			CameraParams m_FrameCamera{};

			// Upload caches (Phase 2 staging uploads, keyed by the CPU object).
			std::unordered_map<const class Mesh*, MeshGpu> m_MeshCache;
			std::unordered_map<const class Texture*, TextureGpu> m_TextureCache;
		};

	}

}
