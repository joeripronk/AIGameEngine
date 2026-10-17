#pragma once

#include "Forge/Renderer/RendererAPI.h"

#include <vulkan/vulkan.h>
#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <vector>

namespace Forge {

	class VulkanSwapchain;

	// Owns the low-level Vulkan objects (instance, physical device, logical
	// device, surface, graphics queue) that nvrhi's DeviceDesc is built from,
	// then hands them to nvrhi to create the rendering DeviceHandle.
	class VulkanRendererAPI : public RendererAPI
	{
	public:
		void Init() override;
		void Shutdown() override;
		void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
		void SetClearColor(const glm::vec4& color) override;
		void Clear() override;
		void DrawIndexed(const Ref<class VertexArray>& vertexArray, uint32_t indexCount = 0) override;
		void SetLineWidth(float width) override;

		// Pipeline management
		void SetupSwapchain(void* windowHandle, uint32_t width, uint32_t height) override;

		// Creates (or recreates) every size-dependent render target: the HDR
		// buffer and its depth, the SSAO buffers, the tonemapped buffer and their
		// framebuffers. Sized from the swapchain's actual extent, never from a
		// guess, so each pass's viewport and scissor match its attachment.
		static void CreateRenderTargets(uint32_t width, uint32_t height);

		// Same as CreateRenderTargets, but a no-op when the size is unchanged.
		void ResizeRenderTargets(uint32_t width, uint32_t height) override;

		void BeginFrame();
		void EndFrame();

		// G-buffer management
		void CreateGBuffer(uint32_t width, uint32_t height);
		void CreateShadowMap(uint32_t width, uint32_t height);
		static void CreateHDRBuffer(uint32_t width, uint32_t height);
		static void CreateSSAOBuffer(uint32_t width, uint32_t height);

		// Post-processing pipeline creation
		void CreatePBRPipeline();
		static void CreateSSAOPipeline();
		static void CreateHDRPipeline();
		static void CreateFinalPipeline();
		void CreateShadowPipeline();

		// Post-processing execution
		static void RenderSSAOPass(nvrhi::ICommandList* commandList, nvrhi::TextureHandle depthTexture, const glm::mat4& invViewProj);
		static void RenderHDRPass(nvrhi::ICommandList* commandList);
		static void RenderFinalPass(nvrhi::ICommandList* commandList, nvrhi::FramebufferHandle swapchainFramebuffer);

		// Descriptor set management
		void UpdateDescriptorSets();

		// Accessors
		nvrhi::DeviceHandle GetDevice() const { return s_Device; }
		nvrhi::CommandListHandle GetCommandList() { return s_CommandList; }
		static nvrhi::DeviceHandle GetStaticDevice() { return s_Device; }
		static nvrhi::CommandListHandle GetStaticCommandList() { return s_CommandList; }

		static VkInstance GetInstance() { return s_Instance; }
		static VkSurfaceKHR GetSurface() { return s_Surface; }
		static VkPhysicalDevice GetPhysicalDevice() { return s_PhysicalDevice; }
		static VkDevice GetLogicalDevice() { return s_LogicalDevice; }
		static VkQueue GetGraphicsQueue() { return s_GraphicsQueue; }
		static uint32_t GetGraphicsQueueIndex() { return s_GraphicsQueueIndex; }

		// Command pool for the graphics queue. Used by the swapchain readback,
		// which records its own one-shot command buffer outside nvrhi.
		static VkCommandPool GetCommandPool() { return s_CommandPool; }

		// Presentation swapchain, created in SetupSwapchain().
		static VulkanSwapchain& GetSwapchain() { return s_Swapchain; }

		// Get the current VkCommandBuffer for ImGui rendering.
		static VkCommandBuffer GetCurrentCommandBuffer();

		// Blocks until the device has finished all submitted work. Used to
		// synchronize nvrhi's command list submission with swapchain
		// acquire/present (nvrhi owns its own queue semaphores).
		static void WaitForGpuIdle();

		// Translates a VkFormat to the matching nvrhi format.
		static nvrhi::Format ToNvrhiFormat(VkFormat format);

		// Post-processing framebuffers (created in SetupSwapchain).
		static nvrhi::FramebufferHandle GetTonemappedFramebuffer() { return s_TonemappedFramebuffer; }
		static nvrhi::FramebufferHandle GetSSAOFramebuffer() { return s_SSAOFramebuffer; }
		static nvrhi::FramebufferHandle GetHDRFramebuffer() { return s_HDRFramebuffer; }

		// HDR buffer and SSAO output accessors.
		static nvrhi::TextureHandle GetHDRBuffer() { return s_HDRBuffer; }
		static nvrhi::TextureHandle GetSSAOOutput() { return s_SSAOOutput; }
		static nvrhi::TextureHandle GetTonemappedBuffer() { return s_TonemappedBuffer; }

		// Post-processing pipeline accessors.
		static nvrhi::GraphicsPipelineHandle GetSSAOPipeline() { return s_SSAOPipeline; }
		static nvrhi::GraphicsPipelineHandle GetHDRPipeline() { return s_HDRPipeline; }
		static nvrhi::GraphicsPipelineHandle GetFinalPipeline() { return s_FinalPipeline; }

		// Post-processing binding layout accessors.
		static nvrhi::BindingLayoutHandle GetSSAOBindingLayout() { return s_SSAOBindingLayout; }
		static nvrhi::BindingLayoutHandle GetHDRBindingLayout() { return s_HDRBindingLayout; }
		static nvrhi::BindingLayoutHandle GetFinalBindingLayout() { return s_FinalBindingLayout; }

		// Post-processing uniform buffer accessors.
		static nvrhi::BufferHandle GetSSAOUniformBuffer() { return s_SSAOUniformBuffer; }
		static nvrhi::BufferHandle GetHDRUniformBuffer() { return s_HDRUniformBuffer; }
		static nvrhi::BufferHandle GetFinalUniformBuffer() { return s_FinalUniformBuffer; }

	private:
		// --- Vulkan bootstrap helpers (static; used during Init) ---
		static bool CreateInstance();
		static bool PickPhysicalDevice();
		static bool CreateLogicalDevice();
		static void DestroyVulkanObjects();

		static VkInstance s_Instance;
		static VkPhysicalDevice s_PhysicalDevice;
		static VkDevice s_LogicalDevice;
		static VkSurfaceKHR s_Surface;
		static VkQueue s_GraphicsQueue;
		static VkCommandPool s_CommandPool;
		static uint32_t s_GraphicsQueueIndex;
		static bool s_ValidationEnabled;

		static nvrhi::DeviceHandle s_Device;
		static nvrhi::CommandListHandle s_CommandList;
		static VulkanSwapchain s_Swapchain;
		static uint32_t s_Width;
		static uint32_t s_Height;

		// Post-processing framebuffers.
		static nvrhi::FramebufferHandle s_TonemappedFramebuffer;
		static nvrhi::FramebufferHandle s_SSAOFramebuffer;

		// HDR buffer and framebuffer.
		static nvrhi::TextureHandle s_HDRBuffer;
		static nvrhi::FramebufferHandle s_HDRFramebuffer;

		// SSAO buffer.
		static nvrhi::TextureHandle s_SSAOOutput;

		// Tonemapped buffer.
		static nvrhi::TextureHandle s_TonemappedBuffer;

		// Post-processing binding layouts (created during pipeline setup).
		static nvrhi::BindingLayoutHandle s_SSAOBindingLayout;
		static nvrhi::BindingLayoutHandle s_HDRBindingLayout;
		static nvrhi::BindingLayoutHandle s_FinalBindingLayout;

		// Post-processing pipelines.
		static nvrhi::GraphicsPipelineHandle s_SSAOPipeline;
		static nvrhi::GraphicsPipelineHandle s_HDRPipeline;
		static nvrhi::GraphicsPipelineHandle s_FinalPipeline;

		// SSAO uniform buffer.
		static nvrhi::BufferHandle s_SSAOUniformBuffer;

		// HDR uniform buffer.
		static nvrhi::BufferHandle s_HDRUniformBuffer;

		// Final uniform buffer.
		static nvrhi::BufferHandle s_FinalUniformBuffer;

		// Depth attachment for the HDR target, and the SSAO pass's depth input.
		static nvrhi::TextureHandle s_HDRDepth;
		static nvrhi::TextureHandle s_SSAOInput;

		// Final composite output.
		static nvrhi::TextureHandle s_FinalBuffer;

		// Vertex layout for the fullscreen quad shared by the post passes.
		static nvrhi::InputLayoutHandle s_FullscreenInputLayout;
	};
	}
