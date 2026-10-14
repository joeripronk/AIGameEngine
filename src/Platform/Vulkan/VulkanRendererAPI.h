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
		void BeginFrame();
		void EndFrame();

		// G-buffer management
		void CreateGBuffer(uint32_t width, uint32_t height);
		void CreateShadowMap(uint32_t width, uint32_t height);
		void CreateHDRBuffer(uint32_t width, uint32_t height);
		void CreateSSAOBuffer(uint32_t width, uint32_t height);

		// Pipeline creation
		void CreatePBRPipeline();
		void CreateSSAOPipeline();
		void CreateHDRPipeline();
		void CreateFinalPipeline();
		void CreateShadowPipeline();

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

		// Command pool for the graphics queue. Used by the swapchain readback,
		// which records its own one-shot command buffer outside nvrhi.
		static VkCommandPool GetCommandPool() { return s_CommandPool; }

		// Presentation swapchain, created in SetupSwapchain().
		static VulkanSwapchain& GetSwapchain() { return s_Swapchain; }

		// Blocks until the device has finished all submitted work. Used to
		// synchronize nvrhi's command list submission with swapchain
		// acquire/present (nvrhi owns its own queue semaphores).
		static void WaitForGpuIdle();

		// Translates a VkFormat to the matching nvrhi format.
		static nvrhi::Format ToNvrhiFormat(VkFormat format);

		// Post-processing framebuffers (created in SetupSwapchain).
		static nvrhi::FramebufferHandle GetTonemappedFramebuffer() { return s_TonemappedFramebuffer; }
		static nvrhi::FramebufferHandle GetSSAOFramebuffer() { return s_SSAOFramebuffer; }

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
	};

}