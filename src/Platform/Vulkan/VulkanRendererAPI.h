#pragma once

#include "Forge/Renderer/RendererAPI.h"
#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

namespace Forge {

	class VulkanRendererAPI : public RendererAPI
	{
	public:
		void Init() override;
		void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
		void SetClearColor(const glm::vec4& color) override;
		void Clear() override;
		void DrawIndexed(const Ref<class VertexArray>& vertexArray, uint32_t indexCount = 0) override;
		void SetLineWidth(float width) override;

		// Pipeline management
		void SetupSwapchain(void* windowHandle, uint32_t width, uint32_t height);
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

	private:
		static nvrhi::DeviceHandle s_Device;
		static nvrhi::CommandListHandle s_CommandList;
		static uint32_t s_Width;
		static uint32_t s_Height;
	};

}
