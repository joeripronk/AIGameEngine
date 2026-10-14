#include "fgpch.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Forge/Renderer/Renderer.h"
#include "Forge/Renderer/RendererAPI.h"
#include "Platform/Vulkan/VulkanSwapchain.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <algorithm>
#include <cstring>

#include <GLFW/glfw3.h>

namespace Forge {

	namespace {

		// Routes nvrhi diagnostics into the engine's log. Without this, nvrhi
		// dereferences a null callback when it needs to report a problem.
		class NvrhiMessageCallback : public nvrhi::IMessageCallback
		{
		public:
			virtual void message(nvrhi::MessageSeverity severity, const char* messageText) override
			{
				switch (severity)
				{
				case nvrhi::MessageSeverity::Warning:
					FG_CORE_WARN("nvrhi: {0}", messageText);
					break;
				case nvrhi::MessageSeverity::Error:
				case nvrhi::MessageSeverity::Fatal:
					FG_CORE_ERROR("nvrhi: {0}", messageText);
					break;
				default:
					FG_CORE_INFO("nvrhi: {0}", messageText);
					break;
				}
			}
		};

		NvrhiMessageCallback s_NvrhiMessageCallback;

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

		// Pipelines
		nvrhi::GraphicsPipelineHandle s_GeometryPipeline;
		nvrhi::GraphicsPipelineHandle s_SSAOPipeline;
		nvrhi::GraphicsPipelineHandle s_HDRPipeline;
		nvrhi::GraphicsPipelineHandle s_TonemapPipeline;
		nvrhi::GraphicsPipelineHandle s_ShadowPipeline;
		nvrhi::GraphicsPipelineHandle s_FinalPipeline;

	}

	// Static member definitions
	VkInstance VulkanRendererAPI::s_Instance = VK_NULL_HANDLE;
	VkPhysicalDevice VulkanRendererAPI::s_PhysicalDevice = VK_NULL_HANDLE;
	VkDevice VulkanRendererAPI::s_LogicalDevice = VK_NULL_HANDLE;
	VkSurfaceKHR VulkanRendererAPI::s_Surface = VK_NULL_HANDLE;
	VkQueue VulkanRendererAPI::s_GraphicsQueue = VK_NULL_HANDLE;
	VkCommandPool VulkanRendererAPI::s_CommandPool = VK_NULL_HANDLE;
	uint32_t VulkanRendererAPI::s_GraphicsQueueIndex = 0;
	bool VulkanRendererAPI::s_ValidationEnabled = false;

	nvrhi::DeviceHandle VulkanRendererAPI::s_Device;
	nvrhi::CommandListHandle VulkanRendererAPI::s_CommandList;
	VulkanSwapchain VulkanRendererAPI::s_Swapchain;
	uint32_t VulkanRendererAPI::s_Width = 0;
	uint32_t VulkanRendererAPI::s_Height = 0;
	nvrhi::FramebufferHandle VulkanRendererAPI::s_TonemappedFramebuffer;
	nvrhi::FramebufferHandle VulkanRendererAPI::s_SSAOFramebuffer;

	bool VulkanRendererAPI::CreateInstance()
	{
		uint32_t extensionCount = 0;
		const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

		std::vector<const char*> instanceExtensions;
		if (extensions)
		{
			for (uint32_t i = 0; i < extensionCount; ++i)
				instanceExtensions.push_back(extensions[i]);
		}

		// Optional validation layer (enabled when available, e.g. dev/test).
		const char* validationLayer = "VK_LAYER_KHRONOS_validation";
		uint32_t layerCount = 0;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
		std::vector<VkLayerProperties> layers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, layers.data());

		bool hasValidation = false;
		for (const auto& layer : layers)
		{
			if (strcmp(layer.layerName, validationLayer) == 0)
			{
				hasValidation = true;
				break;
			}
		}

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Forge Engine";
		appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
		appInfo.pEngineName = "Forge";
		appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
		appInfo.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;
		createInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
		createInfo.ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data();
		createInfo.enabledLayerCount = hasValidation ? 1u : 0u;
		createInfo.ppEnabledLayerNames = hasValidation ? &validationLayer : nullptr;

		VkResult result = vkCreateInstance(&createInfo, nullptr, &s_Instance);
		if (result != VK_SUCCESS)
		{
			FG_CORE_ERROR("Vulkan: vkCreateInstance failed ({0})", (int)result);
			return false;
		}

		s_ValidationEnabled = hasValidation;
		return true;
	}

	bool VulkanRendererAPI::PickPhysicalDevice()
	{
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices(s_Instance, &deviceCount, nullptr);
		if (deviceCount == 0)
		{
			FG_CORE_ERROR("Vulkan: no physical devices found");
			return false;
		}

		std::vector<VkPhysicalDevice> devices(deviceCount);
		vkEnumeratePhysicalDevices(s_Instance, &deviceCount, devices.data());

		// Prefer a discrete GPU, then integrated, then anything (e.g. software).
		int bestScore = -1;
		s_PhysicalDevice = VK_NULL_HANDLE;

		for (VkPhysicalDevice candidate : devices)
		{
			VkPhysicalDeviceProperties props{};
			vkGetPhysicalDeviceProperties(candidate, &props);

			// Find a queue family that supports graphics.
			uint32_t queueFamilyCount = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
			std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
			vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, families.data());

			uint32_t graphicsFamily = UINT32_MAX;
			for (uint32_t i = 0; i < queueFamilyCount; ++i)
			{
				if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					graphicsFamily = i;
					break;
				}
			}

			if (graphicsFamily == UINT32_MAX)
				continue;

			int score = 0;
			if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
				score = 3;
			else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
				score = 2;
			else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU)
				score = 1;

			if (score > bestScore)
			{
				bestScore = score;
				s_PhysicalDevice = candidate;
				s_GraphicsQueueIndex = graphicsFamily;
			}
		}

		if (s_PhysicalDevice == VK_NULL_HANDLE)
		{
			FG_CORE_ERROR("Vulkan: no device with a graphics queue");
			return false;
		}

		VkPhysicalDeviceProperties props{};
		vkGetPhysicalDeviceProperties(s_PhysicalDevice, &props);
		FG_CORE_INFO("Vulkan: selected physical device '{0}'", props.deviceName);
		return true;
	}

	bool VulkanRendererAPI::CreateLogicalDevice()
	{
		// Enable the swapchain extension (required for presentation).
		uint32_t extensionCount = 0;
		vkEnumerateDeviceExtensionProperties(s_PhysicalDevice, nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> available(extensionCount);
		vkEnumerateDeviceExtensionProperties(s_PhysicalDevice, nullptr, &extensionCount, available.data());

		bool hasSwapchain = false;
		for (const auto& ext : available)
		{
			if (strcmp(ext.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
			{
				hasSwapchain = true;
				break;
			}
		}

		if (!hasSwapchain)
		{
			FG_CORE_ERROR("Vulkan: device does not support swapchain");
			return false;
		}

		const char* deviceExtensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

		float queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo{};
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = s_GraphicsQueueIndex;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &queuePriority;

		VkDeviceCreateInfo deviceInfo{};
		deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = 1;
		deviceInfo.ppEnabledExtensionNames = deviceExtensions;

		VkResult result = vkCreateDevice(s_PhysicalDevice, &deviceInfo, nullptr, &s_LogicalDevice);
		if (result != VK_SUCCESS)
		{
			FG_CORE_ERROR("Vulkan: vkCreateDevice failed ({0})", (int)result);
			return false;
		}

		vkGetDeviceQueue(s_LogicalDevice, s_GraphicsQueueIndex, 0, &s_GraphicsQueue);

		// A command pool for one-shot command buffers recorded outside nvrhi
		// (currently the swapchain image readback).
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
		poolInfo.queueFamilyIndex = s_GraphicsQueueIndex;
		if (vkCreateCommandPool(s_LogicalDevice, &poolInfo, nullptr, &s_CommandPool) != VK_SUCCESS)
		{
			FG_CORE_ERROR("Vulkan: vkCreateCommandPool failed");
			s_CommandPool = VK_NULL_HANDLE;
		}
		return true;
	}

	void VulkanRendererAPI::DestroyVulkanObjects()
	{
		if (s_Surface != VK_NULL_HANDLE)
		{
			vkDestroySurfaceKHR(s_Instance, s_Surface, nullptr);
			s_Surface = VK_NULL_HANDLE;
		}

		if (s_CommandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(s_LogicalDevice, s_CommandPool, nullptr);
			s_CommandPool = VK_NULL_HANDLE;
		}

		if (s_LogicalDevice != VK_NULL_HANDLE)
		{
			vkDestroyDevice(s_LogicalDevice, nullptr);
			s_LogicalDevice = VK_NULL_HANDLE;
		}

		if (s_Instance != VK_NULL_HANDLE)
		{
			vkDestroyInstance(s_Instance, nullptr);
			s_Instance = VK_NULL_HANDLE;
		}

		s_PhysicalDevice = VK_NULL_HANDLE;
		s_GraphicsQueue = VK_NULL_HANDLE;
	}

	void VulkanRendererAPI::Init()
	{
		if (s_Device)
			return;

		if (!CreateInstance())
			return;
		if (!PickPhysicalDevice())
		{
			DestroyVulkanObjects();
			return;
		}
		if (!CreateLogicalDevice())
		{
			DestroyVulkanObjects();
			return;
		}

		// Hand the low-level objects to nvrhi to build its rendering device.
		nvrhi::vulkan::DeviceDesc deviceDesc;
		deviceDesc.instance = s_Instance;
		deviceDesc.physicalDevice = s_PhysicalDevice;
		deviceDesc.device = s_LogicalDevice;
		deviceDesc.graphicsQueue = s_GraphicsQueue;
		deviceDesc.graphicsQueueIndex = (int)s_GraphicsQueueIndex;
		deviceDesc.errorCB = &s_NvrhiMessageCallback;

		s_Device = nvrhi::vulkan::createDevice(deviceDesc);
		if (!s_Device)
		{
			FG_CORE_ERROR("Vulkan: nvrhi::vulkan::createDevice returned null");
			DestroyVulkanObjects();
			return;
		}

		// Create a command list the renderer records into.
		nvrhi::CommandListParameters commandListDesc;
		s_CommandList = s_Device->createCommandList(commandListDesc);

		FG_CORE_INFO("VulkanRendererAPI initialized");
	}

	void VulkanRendererAPI::Shutdown()
	{
		s_Swapchain.Destroy();
		s_CommandList = nullptr;
		s_Device = nullptr;
		DestroyVulkanObjects();
		FG_CORE_INFO("VulkanRendererAPI shut down");
	}

	void VulkanRendererAPI::WaitForGpuIdle()
	{
		if (s_Device)
			s_Device->waitForIdle();
	}

	VkCommandBuffer VulkanRendererAPI::GetCurrentCommandBuffer()
	{
		if (!s_CommandList)
			return VK_NULL_HANDLE;

		// Get the native VkCommandBuffer from the nvrhi command list.
		return static_cast<VkCommandBuffer>(s_CommandList->getNativeObject(nvrhi::ObjectTypes::VK_CommandBuffer));
	}

	nvrhi::Format VulkanRendererAPI::ToNvrhiFormat(VkFormat format)
	{
		switch (format)
		{
		case VK_FORMAT_R8G8B8A8_UNORM:                 return nvrhi::Format::RGBA8_UNORM;
		case VK_FORMAT_R8G8B8A8_SRGB:                  return nvrhi::Format::SRGBA8_UNORM;
		case VK_FORMAT_B8G8R8A8_UNORM:                 return nvrhi::Format::BGRA8_UNORM;
		case VK_FORMAT_B8G8R8A8_SRGB:                  return nvrhi::Format::SBGRA8_UNORM;
		case VK_FORMAT_R16G16B16A16_SFLOAT:            return nvrhi::Format::RGBA16_FLOAT;
		case VK_FORMAT_R32G32B32A32_SFLOAT:            return nvrhi::Format::RGBA32_FLOAT;
		case VK_FORMAT_R16G16B16A16_UNORM:             return nvrhi::Format::RGBA16_UNORM;
		case VK_FORMAT_R8_UNORM:                       return nvrhi::Format::R8_UNORM;
		case VK_FORMAT_D32_SFLOAT:                     return nvrhi::Format::D32;
		case VK_FORMAT_D24_UNORM_S8_UINT:              return nvrhi::Format::D24S8;
		case VK_FORMAT_D32_SFLOAT_S8_UINT:             return nvrhi::Format::D32S8;
		default:                                       return nvrhi::Format::UNKNOWN;
		}
	}

	void VulkanRendererAPI::SetupSwapchain(void* windowHandle, uint32_t width, uint32_t height)
	{
		s_Width = width;
		s_Height = height;

		if (!s_Instance || !windowHandle)
			return;

		GLFWwindow* glfwWindow = (GLFWwindow*)windowHandle;

		// Create a Vulkan surface for the GLFW window.
		if (s_Surface == VK_NULL_HANDLE)
		{
			if (glfwCreateWindowSurface(s_Instance, glfwWindow, nullptr, &s_Surface) != VK_SUCCESS)
			{
				FG_CORE_ERROR("Vulkan: failed to create window surface");
				return;
			}
		}

		// Create the presentation swapchain (nvrhi has no swapchain abstraction).
		if (!GetSwapchain().Create(glfwWindow, width, height))
		{
			FG_CORE_ERROR("Vulkan: swapchain creation failed; presentation disabled");
			return;
		}

		// Create G-Buffer
		CreateGBuffer(width, height);

		// Create Shadow Map
		CreateShadowMap(2048, 2048);

		// Create HDR Buffer
		CreateHDRBuffer(width, height);

		// Create SSAO Buffer
		CreateSSAOBuffer(width, height);

		// Create Tonemapped buffer
		nvrhi::TextureDesc tonemapDesc;
		tonemapDesc.width = width;
		tonemapDesc.height = height;
		tonemapDesc.format = nvrhi::Format::RGBA16_FLOAT;
		tonemapDesc.isShaderResource = true;
		tonemapDesc.isRenderTarget = true;
		s_TonemappedBuffer = s_Device->createTexture(tonemapDesc);

		// Create Final buffer
		nvrhi::TextureDesc finalDesc;
		finalDesc.width = width;
		finalDesc.height = height;
		finalDesc.format = nvrhi::Format::RGBA8_UNORM;
		finalDesc.isShaderResource = true;
		finalDesc.isRenderTarget = true;
		s_FinalBuffer = s_Device->createTexture(finalDesc);

		// Create post-processing framebuffers.
		// Tonemapped framebuffer (color attachment = tonemapped buffer).
		{
			nvrhi::FramebufferDesc tonemappedFbDesc;
			tonemappedFbDesc.addColorAttachment(s_TonemappedBuffer.Get());
			s_TonemappedFramebuffer = s_Device->createFramebuffer(tonemappedFbDesc);
			if (s_TonemappedFramebuffer)
				FG_CORE_INFO("Tonemapped framebuffer created: {0}x{1}", width, height);
		}

		// SSAO output framebuffer (color attachment = SSAO output buffer).
		{
			nvrhi::FramebufferDesc ssaoFbDesc;
			ssaoFbDesc.addColorAttachment(s_SSAOOutput.Get());
			s_SSAOFramebuffer = s_Device->createFramebuffer(ssaoFbDesc);
			if (s_SSAOFramebuffer)
				FG_CORE_INFO("SSAO framebuffer created: {0}x{1}", width, height);
		}

		// Create UBO
		nvrhi::BufferDesc uboDesc;
		uboDesc.byteSize = 256;
		uboDesc.debugName = "UBO";
		s_UboBuffer = s_Device->createBuffer(uboDesc);

		// Create pipelines
		CreatePBRPipeline();
		CreateSSAOPipeline();
		CreateHDRPipeline();
		CreateFinalPipeline();
		CreateShadowPipeline();

		FG_CORE_INFO("Swapchain resources initialized: {0}x{1}", width, height);
	}

	void VulkanRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		(void)x; (void)y; (void)width; (void)height;
	}

	void VulkanRendererAPI::SetClearColor(const glm::vec4& color)
	{
		(void)color;
	}

	void VulkanRendererAPI::Clear()
	{
		// Simplified clear - pipeline is stubbed
	}

	void VulkanRendererAPI::DrawIndexed(const Ref<class VertexArray>& vertexArray, uint32_t indexCount)
	{
		// Simplified draw - pipeline is stubbed
		(void)vertexArray; (void)indexCount;
	}

	void VulkanRendererAPI::SetLineWidth(float width)
	{
		(void)width;
	}

	void VulkanRendererAPI::CreateGBuffer(uint32_t width, uint32_t height)
	{
		nvrhi::TextureDesc gbuf0;
		gbuf0.width = width;
		gbuf0.height = height;
		gbuf0.format = nvrhi::Format::RGBA32_FLOAT;
		gbuf0.isShaderResource = true;
		gbuf0.isRenderTarget = true;
		s_GBuffer0 = s_Device->createTexture(gbuf0);

		nvrhi::TextureDesc gbuf1;
		gbuf1.width = width;
		gbuf1.height = height;
		gbuf1.format = nvrhi::Format::RGBA32_FLOAT;
		gbuf1.isShaderResource = true;
		gbuf1.isRenderTarget = true;
		s_GBuffer1 = s_Device->createTexture(gbuf1);

		nvrhi::TextureDesc gbuf2;
		gbuf2.width = width;
		gbuf2.height = height;
		gbuf2.format = nvrhi::Format::RGBA32_FLOAT;
		gbuf2.isShaderResource = true;
		gbuf2.isRenderTarget = true;
		s_GBuffer2 = s_Device->createTexture(gbuf2);

		nvrhi::TextureDesc gbuf3;
		gbuf3.width = width;
		gbuf3.height = height;
		gbuf3.format = nvrhi::Format::RGBA32_FLOAT;
		gbuf3.isShaderResource = true;
		gbuf3.isRenderTarget = true;
		s_GBuffer3 = s_Device->createTexture(gbuf3);

		FG_CORE_INFO("G-Buffer created: {0}x{1}", width, height);
	}

	void VulkanRendererAPI::CreateShadowMap(uint32_t width, uint32_t height)
	{
		(void)width; (void)height;
		FG_CORE_INFO("Shadow map created (stub)");
	}

	void VulkanRendererAPI::CreateHDRBuffer(uint32_t width, uint32_t height)
	{
		nvrhi::TextureDesc hdrDesc;
		hdrDesc.width = width;
		hdrDesc.height = height;
		hdrDesc.format = nvrhi::Format::RGBA16_FLOAT;
		hdrDesc.isShaderResource = true;
		hdrDesc.isRenderTarget = true;
		s_HDRBuffer = s_Device->createTexture(hdrDesc);

		FG_CORE_INFO("HDR buffer created: {0}x{1}", width, height);
	}

	void VulkanRendererAPI::CreateSSAOBuffer(uint32_t width, uint32_t height)
	{
		nvrhi::TextureDesc ssaoInput;
		ssaoInput.width = width;
		ssaoInput.height = height;
		ssaoInput.format = nvrhi::Format::RGBA32_FLOAT;
		ssaoInput.isShaderResource = true;
		ssaoInput.isRenderTarget = true;
		s_SSAOInput = s_Device->createTexture(ssaoInput);

		nvrhi::TextureDesc ssaoOutput;
		ssaoOutput.width = width;
		ssaoOutput.height = height;
		ssaoOutput.format = nvrhi::Format::R8_UNORM;
		ssaoOutput.isShaderResource = true;
		ssaoOutput.isRenderTarget = true;
		s_SSAOOutput = s_Device->createTexture(ssaoOutput);

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
		// Simplified - pipeline is stubbed
	}

	void VulkanRendererAPI::EndFrame()
	{
		// Simplified - pipeline is stubbed
	}

}
#endif // FG_NO_VULKAN_RUNTIME
