#include "fgpch.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include "Forge/Renderer/Renderer.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/common.h>
#include <nvrhi/shaders.h>
#include <nvrhi/texture.h>
#include <nvrhi/buffer.h>
#include <nvrhi/swapchain.h>
#include <nvrhi/query.h>
#include <nvrhi/descriptor_set.h>
#include <nvrhi/pipeline.h>

#include "Forge/Renderer/VertexArray.h"
#include "Forge/Renderer/VertexBuffer.h"
#include "Forge/Renderer/IndexBuffer.h"

namespace Forge {

	namespace {
		nvrhi::DeviceHandle s_Device;
		nvrhi::CommandListHandle s_CommandList;
		nvrhi::SwapchainHandle s_Swapchain;
		uint32_t s_Width = 1920;
		uint32_t s_Height = 1080;

		// G-Buffer textures
		nvrhi::TextureHandle s_GBuffer0; // Position
		nvrhi::TextureHandle s_GBuffer1; // Normal
		nvrhi::TextureHandle s_GBuffer2; // Material (Albedo, Metallic, Roughness, AO)
		nvrhi::TextureHandle s_GBuffer3; // Emissive

		// Shadow map
		nvrhi::TextureHandle s_ShadowMap;
		nvrhi::TextureHandle s_ShadowDepth;

		// HDR buffer
		nvrhi::TextureHandle s_HDRBuffer;

		// SSAO buffer
		nvrhi::TextureHandle s_SSAOInput;
		nvrhi::TextureHandle s_SSAOOutput;

		// Tonemapped buffer
		nvrhi::TextureHandle s_TonemappedBuffer;

		// Final buffer
		nvrhi::TextureHandle s_FinalBuffer;

		// UBO
		nvrhi::BufferHandle s_UboBuffer;

		// Descriptor sets
		nvrhi::DescriptorSetHandle s_GeometryDS;
		nvrhi::DescriptorSetHandle s_SSAODS;
		nvrhi::DescriptorSetHandle s_HDRDS;
		nvrhi::DescriptorSetHandle s_TonemapDS;
		nvrhi::DescriptorSetHandle s_ShadowDS;
		nvrhi::DescriptorSetHandle s_FinalDS;

		// Pipelines
		nvrhi::PipelineHandle s_GeometryPipeline;
		nvrhi::PipelineHandle s_SSAOPipeline;
		nvrhi::PipelineHandle s_HDRPipeline;
		nvrhi::PipelineHandle s_TonemapPipeline;
		nvrhi::PipelineHandle s_ShadowPipeline;
		nvrhi::PipelineHandle s_FinalPipeline;
	}

	void VulkanRendererAPI::Init()
	{
		nvrhi::vulkan::CreateDeviceDesc desc;
		desc.pApplicationName = "Forge Engine";
		desc.pEngineName = "Forge Engine";
		desc.pDeviceName = "Forge";
		s_Device = nvrhi::vulkan::CreateDevice(desc);

		s_CommandList = s_Device->createCommandList();
		s_CommandList->finish();

		FG_CORE_INFO("VulkanRendererAPI initialized");
	}

	void VulkanRendererAPI::SetupSwapchain(void* windowHandle, uint32_t width, uint32_t height)
	{
		s_Width = width;
		s_Height = height;

		nvrhi::SwapchainDesc swapchainDesc;
		swapchainDesc.window = windowHandle;
		swapchainDesc.width = width;
		swapchainDesc.height = height;
		swapchainDesc.format = nvrhi::TextureFormat::RGBA8_UNorm;
		swapchainDesc.isSrgb = true;
		swapchainDesc.presentMode = nvrhi::PresentMode::Fifo;

		s_Swapchain = s_Device->createSwapchain(swapchainDesc);

		// Create G-Buffer
		CreateGBuffer(width, height);

		// Create Shadow Map
		CreateShadowMap(2048, 2048);

		// Create HDR Buffer
		CreateHDRBuffer(width, height);

		// Create SSAO Buffer
		CreateSSAOBuffer(width, height);

		// Create Tonemapped Buffer
		s_TonemappedBuffer = s_Device->createTexture(nvrhi::TextureDesc()
			.withWidth(width).withHeight(height)
			.withFormat(nvrhi::TextureFormat::RGBA16_Float)
			.withUsage(nvrhi::TextureUsageFlags::ShaderResource | nvrhi::TextureUsageFlags::RenderTarget));

		// Create Final Buffer
		s_FinalBuffer = s_Device->createTexture(nvrhi::TextureDesc()
			.withWidth(width).withHeight(height)
			.withFormat(nvrhi::TextureFormat::RGBA8_UNorm)
			.withUsage(nvrhi::TextureUsageFlags::ShaderResource | nvrhi::TextureUsageFlags::RenderTarget));

		// Create UBO
		s_UboBuffer = s_Device->createBuffer(nvrhi::BufferDesc()
			.withSize(256)
			.withUsage(nvrhi::BufferUsageFlags::Uniform | nvrhi::BufferUsageFlags::CopyDest));

		// Create pipelines
		CreatePBRPipeline();
		CreateSSAOPipeline();
		CreateHDRPipeline();
		CreateFinalPipeline();
		CreateShadowPipeline();

		FG_CORE_INFO("Swapchain initialized: {0}x{1}", width, height);
	}

	void VulkanRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		s_Width = width;
		s_Height = height;
	}

	void VulkanRendererAPI::SetClearColor(const glm::vec4& color)
	{
	}

	void VulkanRendererAPI::Clear()
	{
		s_CommandList->begin();
		s_CommandList->setViewport(0, 0, (float)s_Width, (float)s_Height, 0.0f, 1.0f);
		s_CommandList->setScissor(0, 0, s_Width, s_Height);

		nvrhi::ClearColorDesc clearDesc;
		clearDesc.colors[0] = nvrhi::ColorF(0.0f, 0.0f, 0.0f, 1.0f);
		s_CommandList->clearRenderTargets({ s_Swapchain->getTexture(0) }, clearDesc, 0.0f, 0);

		s_CommandList->finish();
	}

	void VulkanRendererAPI::DrawIndexed(const Ref<class VertexArray>& vertexArray, uint32_t indexCount)
	{
		vertexArray->Bind();

		auto ib = vertexArray->GetIndexBuffer();
		if (ib)
		{
			uint32_t count = indexCount > 0 ? indexCount : ib->GetCount();
			s_CommandList->drawIndexed(count, 1, 0, 0, 0);
		}
		else
		{
			s_CommandList->draw(0, vertexArray->GetVertexBuffers()[0]->GetSize() / sizeof(float), 0, 0, 0);
		}
	}

	void VulkanRendererAPI::SetLineWidth(float width)
	{
	}

	void VulkanRendererAPI::CreateGBuffer(uint32_t width, uint32_t height)
	{
		s_GBuffer0 = s_Device->createTexture(nvrhi::TextureDesc()
			.withWidth(width).withHeight(height)
			.withFormat(nvrhi::TextureFormat::RGBA32_Float)
			.withUsage(nvrhi::TextureUsageFlags::ShaderResource | nvrhi::TextureUsageFlags::RenderTarget));

		s_GBuffer1 = s_Device->createTexture(nvrhi::TextureDesc()
			.withWidth(width).withHeight(height)
			.withFormat(nvrhi::TextureFormat::RGBA32_Float)
			.withUsage(nvrhi::TextureUsageFlags::ShaderResource | nvrhi::TextureUsageFlags::RenderTarget));

		s_GBuffer2 = s_Device->createTexture(nvrhi::TextureDesc()
			.withWidth(width).withHeight(height)
			.withFormat(nvrhi::TextureFormat::RGBA32_Float)
			.withUsage(nvrhi::TextureUsageFlags::ShaderResource | nvrhi::TextureUsageFlags::RenderTarget));

		s_GBuffer3 = s_Device->createTexture(nvrhi::TextureDesc()
			.withWidth(width).withHeight(height)
			.withFormat(nvrhi::TextureFormat::RGBA32_Float)
			.withUsage(nvrhi::TextureUsageFlags::ShaderResource | nvrhi::TextureUsageFlags::RenderTarget));

		FG_CORE_INFO("G-Buffer created: {0}x{1}", width, height);
	}

	void VulkanRendererAPI::CreateShadowMap(uint32_t width, uint32_t height)
	{
		s_ShadowMap = s_Device->createTexture(nvrhi::TextureDesc()
			.withWidth(width).withHeight(height)
			.withFormat(nvrhi::TextureFormat::D32_Float)
			.withUsage(nvrhi::TextureUsageFlags::ShaderResource | nvrhi::TextureUsageFlags::DepthStencil));

		FG_CORE_INFO("Shadow map created: {0}x{1}", width, height);
	}

	void VulkanRendererAPI::CreateHDRBuffer(uint32_t width, uint32_t height)
	{
		s_HDRBuffer = s_Device->createTexture(nvrhi::TextureDesc()
			.withWidth(width).withHeight(height)
			.withFormat(nvrhi::TextureFormat::RGBA16_Float)
			.withUsage(nvrhi::TextureUsageFlags::ShaderResource | nvrhi::TextureUsageFlags::RenderTarget));

		FG_CORE_INFO("HDR buffer created: {0}x{1}", width, height);
	}

	void VulkanRendererAPI::CreateSSAOBuffer(uint32_t width, uint32_t height)
	{
		s_SSAOInput = s_Device->createTexture(nvrhi::TextureDesc()
			.withWidth(width).withHeight(height)
			.withFormat(nvrhi::TextureFormat::RGBA32_Float)
			.withUsage(nvrhi::TextureUsageFlags::ShaderResource | nvrhi::TextureUsageFlags::RenderTarget));

		s_SSAOOutput = s_Device->createTexture(nvrhi::TextureDesc()
			.withWidth(width).withHeight(height)
			.withFormat(nvrhi::TextureFormat::R8_UNorm)
			.withUsage(nvrhi::TextureUsageFlags::ShaderResource | nvrhi::TextureUsageFlags::RenderTarget));

		FG_CORE_INFO("SSAO buffer created: {0}x{1}", width, height);
	}

	void VulkanRendererAPI::CreatePBRPipeline()
	{
		// TODO: Create PBR geometry pipeline
		// This requires shader bytecode, descriptor layout, and pipeline state
		FG_CORE_INFO("PBR pipeline created (stub)");
	}

	void VulkanRendererAPI::CreateSSAOPipeline()
	{
		// TODO: Create SSAO pipeline
		FG_CORE_INFO("SSAO pipeline created (stub)");
	}

	void VulkanRendererAPI::CreateHDRPipeline()
	{
		// TODO: Create HDR tonemapping pipeline
		FG_CORE_INFO("HDR pipeline created (stub)");
	}

	void VulkanRendererAPI::CreateFinalPipeline()
	{
		// TODO: Create final composite pipeline
		FG_CORE_INFO("Final pipeline created (stub)");
	}

	void VulkanRendererAPI::CreateShadowPipeline()
	{
		// TODO: Create shadow map pipeline
		FG_CORE_INFO("Shadow pipeline created (stub)");
	}

	void VulkanRendererAPI::UpdateDescriptorSets()
	{
		// TODO: Update descriptor sets with current scene data
	}

	void VulkanRendererAPI::BeginFrame()
	{
		s_CommandList->begin();
		s_CommandList->setViewport(0, 0, (float)s_Width, (float)s_Height, 0.0f, 1.0f);
		s_CommandList->setScissor(0, 0, s_Width, s_Height);
	}

	void VulkanRendererAPI::EndFrame()
	{
		s_CommandList->finish();
	}

}
