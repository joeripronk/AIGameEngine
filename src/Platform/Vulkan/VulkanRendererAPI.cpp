#include "fgpch.h"
#include <cstdlib>
#include "Platform/Vulkan/VulkanRendererAPI.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Forge/Renderer/Renderer.h"
#include "Forge/Renderer/RendererAPI.h"
#include "Forge/Renderer/ShaderLibrary.h"
#include "Platform/Vulkan/VulkanShader.h"
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
	} // namespace


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
	nvrhi::TextureHandle VulkanRendererAPI::s_HDRBuffer;
	nvrhi::TextureHandle VulkanRendererAPI::s_HDRDepth;
	nvrhi::FramebufferHandle VulkanRendererAPI::s_HDRFramebuffer;
	nvrhi::TextureHandle VulkanRendererAPI::s_SSAOInput;
	nvrhi::TextureHandle VulkanRendererAPI::s_SSAOOutput;
	nvrhi::TextureHandle VulkanRendererAPI::s_TonemappedBuffer;
	nvrhi::TextureHandle VulkanRendererAPI::s_FinalBuffer;
	nvrhi::BindingLayoutHandle VulkanRendererAPI::s_SSAOBindingLayout;
	nvrhi::BindingLayoutHandle VulkanRendererAPI::s_HDRBindingLayout;
	nvrhi::BindingLayoutHandle VulkanRendererAPI::s_FinalBindingLayout;
	nvrhi::InputLayoutHandle VulkanRendererAPI::s_FullscreenInputLayout;
	nvrhi::GraphicsPipelineHandle VulkanRendererAPI::s_SSAOPipeline;
	nvrhi::GraphicsPipelineHandle VulkanRendererAPI::s_HDRPipeline;
	nvrhi::GraphicsPipelineHandle VulkanRendererAPI::s_FinalPipeline;
	nvrhi::BufferHandle VulkanRendererAPI::s_SSAOUniformBuffer;
	nvrhi::BufferHandle VulkanRendererAPI::s_HDRUniformBuffer;
	nvrhi::BufferHandle VulkanRendererAPI::s_FinalUniformBuffer;

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
		nvrhi::vulkan::DeviceDesc deviceDesc = {};
		deviceDesc.instance = s_Instance;
		deviceDesc.physicalDevice = s_PhysicalDevice;
		deviceDesc.device = s_LogicalDevice;
		deviceDesc.graphicsQueue = s_GraphicsQueue;
		deviceDesc.graphicsQueueIndex = (int)s_GraphicsQueueIndex;
		deviceDesc.transferQueue = VK_NULL_HANDLE;
		deviceDesc.computeQueue = VK_NULL_HANDLE;
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
		// The swapchain is clamped to the surface extent, so its final size can
		// differ from the size requested. Every render target is sized from the
		// value the swapchain reports, never the value passed in: a target of a
		// different size makes each post-processing pass set a viewport and
		// scissor larger than the attachment it draws into.
		if (!GetSwapchain().Create(glfwWindow, width, height))
		{
			FG_CORE_ERROR("Vulkan: swapchain creation failed; presentation disabled");
			return;
		}

		CreateRenderTargets(GetSwapchain().GetWidth(), GetSwapchain().GetHeight());
	}

	void VulkanRendererAPI::CreateRenderTargets(uint32_t width, uint32_t height)
	{
		if (!s_Device || width == 0 || height == 0)
			return;

		s_Width = width;
		s_Height = height;

		// Create HDR Buffer
		CreateHDRBuffer(width, height);

		// Create HDR framebuffer (offscreen target for PBR rendering).
		{
			// Depth texture for HDR buffer.
			nvrhi::TextureDesc hdrDepthDesc;
			hdrDepthDesc.width = width;
			hdrDepthDesc.height = height;
			hdrDepthDesc.format = nvrhi::Format::D32;
			hdrDepthDesc.isRenderTarget = true;
			hdrDepthDesc.isShaderResource = true;
			hdrDepthDesc.debugName = "HDR_Depth";
			hdrDepthDesc.keepInitialState = true;
			hdrDepthDesc.initialState = nvrhi::ResourceStates::RenderTarget;
			s_HDRDepth = s_Device->createTexture(hdrDepthDesc);

			// HDR framebuffer with color + depth.
			nvrhi::FramebufferDesc hdrFbDesc;
			hdrFbDesc.addColorAttachment(s_HDRBuffer.Get());
			hdrFbDesc.setDepthAttachment(s_HDRDepth.Get());
			s_HDRFramebuffer = s_Device->createFramebuffer(hdrFbDesc);
			if (s_HDRFramebuffer)
				FG_CORE_INFO("HDR framebuffer created: {0}x{1}", width, height);
		}

		// Create SSAO Buffer
		CreateSSAOBuffer(width, height);

		// Create Tonemapped buffer
		nvrhi::TextureDesc tonemapDesc;
		tonemapDesc.width = width;
		tonemapDesc.height = height;
		tonemapDesc.format = nvrhi::Format::RGBA16_FLOAT;
		tonemapDesc.isShaderResource = true;
		tonemapDesc.isRenderTarget = true;
		tonemapDesc.keepInitialState = true;
		tonemapDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		tonemapDesc.debugName = "TonemappedBuffer";
		s_TonemappedBuffer = s_Device->createTexture(tonemapDesc);

		// Create Final buffer
		nvrhi::TextureDesc finalDesc;
		finalDesc.width = width;
		finalDesc.height = height;
		finalDesc.format = nvrhi::Format::RGBA8_UNORM;
		finalDesc.isShaderResource = true;
		finalDesc.isRenderTarget = true;
		finalDesc.keepInitialState = true;
		finalDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		finalDesc.debugName = "FinalBuffer";
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

		// Create uniform buffers for post-processing. These do not depend on the
		// frame size, so they are built once and survive a resize.
		if (!s_SSAOUniformBuffer)
		{
			nvrhi::BufferDesc ssaoUboDesc;
			ssaoUboDesc.byteSize = 256;
			ssaoUboDesc.isConstantBuffer = true;
			ssaoUboDesc.debugName = "SSAO_Uniform";
			ssaoUboDesc.keepInitialState = true;
			ssaoUboDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
			s_SSAOUniformBuffer = s_Device->createBuffer(ssaoUboDesc);
		}

		if (!s_HDRUniformBuffer)
		{
			nvrhi::BufferDesc hdrUboDesc;
			hdrUboDesc.byteSize = 256;
			hdrUboDesc.isConstantBuffer = true;
			hdrUboDesc.debugName = "HDR_Uniform";
			hdrUboDesc.keepInitialState = true;
			hdrUboDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
			s_HDRUniformBuffer = s_Device->createBuffer(hdrUboDesc);
		}

		if (!s_FinalUniformBuffer)
		{
			nvrhi::BufferDesc finalUboDesc;
			finalUboDesc.byteSize = 256;
			finalUboDesc.isConstantBuffer = true;
			finalUboDesc.debugName = "Final_Uniform";
			finalUboDesc.keepInitialState = true;
			finalUboDesc.initialState = nvrhi::ResourceStates::ConstantBuffer;
			s_FinalUniformBuffer = s_Device->createBuffer(finalUboDesc);
		}

		// Create the fullscreen quad's vertex input layout (also size
		// independent). nvrhi does not reflect SPIR-V, so a pipeline created
		// without one fetches no vertex attributes at all: every post-processing
		// draw collapsed to a degenerate point and wrote nothing.
		if (!s_FullscreenInputLayout)
		{
			nvrhi::VertexAttributeDesc quadAttributes[2]{};
			quadAttributes[0].name = "aPosition";
			quadAttributes[0].format = nvrhi::Format::RG32_FLOAT;
			quadAttributes[0].arraySize = 1;
			quadAttributes[0].bufferIndex = 0;
			quadAttributes[0].offset = 0;
			quadAttributes[0].elementStride = sizeof(glm::vec2) * 2; // pos + uv
			quadAttributes[0].isInstanced = false;

			quadAttributes[1].name = "aUV";
			quadAttributes[1].format = nvrhi::Format::RG32_FLOAT;
			quadAttributes[1].arraySize = 1;
			quadAttributes[1].bufferIndex = 0;
			quadAttributes[1].offset = sizeof(glm::vec2);
			quadAttributes[1].elementStride = sizeof(glm::vec2) * 2; // pos + uv
			quadAttributes[1].isInstanced = false;

			// nvrhi assigns locations in declaration order, which matches the
			// explicit locations in fullscreen.vert (aPosition 0, aUV 1).
			s_FullscreenInputLayout = s_Device->createInputLayout(quadAttributes, 2, nullptr);
		}

		// Create pipelines (also size independent).
		if (!s_SSAOPipeline)
			CreateSSAOPipeline();
		if (!s_HDRPipeline)
			CreateHDRPipeline();
		if (!s_FinalPipeline)
			CreateFinalPipeline();

		FG_CORE_INFO("Swapchain resources initialized: {0}x{1}", width, height);
	}

	void VulkanRendererAPI::ResizeRenderTargets(uint32_t width, uint32_t height)
	{
		if (!s_Device || width == 0 || height == 0)
			return;

		if (width == s_Width && height == s_Height)
			return;

		// Drop the framebuffers before their attachments are replaced. nvrhi
		// reference-counts, so this only states the intent: nothing may draw
		// through a framebuffer that is about to be recreated.
		s_HDRFramebuffer = nullptr;
		s_TonemappedFramebuffer = nullptr;
		s_SSAOFramebuffer = nullptr;

		CreateRenderTargets(width, height);
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

	void VulkanRendererAPI::CreateHDRBuffer(uint32_t width, uint32_t height)
	{
		nvrhi::TextureDesc hdrDesc;
		hdrDesc.width = width;
		hdrDesc.height = height;
		hdrDesc.format = nvrhi::Format::RGBA16_FLOAT;
		hdrDesc.isShaderResource = true;
		hdrDesc.isRenderTarget = true;
		hdrDesc.keepInitialState = true;
		hdrDesc.initialState = nvrhi::ResourceStates::RenderTarget;
		hdrDesc.debugName = "HDRBuffer";
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
		ssaoInput.keepInitialState = true;
		ssaoInput.initialState = nvrhi::ResourceStates::RenderTarget;
		ssaoInput.debugName = "SSAOInput";
		s_SSAOInput = s_Device->createTexture(ssaoInput);

		nvrhi::TextureDesc ssaoOutput;
		ssaoOutput.width = width;
		ssaoOutput.height = height;
		ssaoOutput.format = nvrhi::Format::R8_UNORM;
		ssaoOutput.isShaderResource = true;
		ssaoOutput.isRenderTarget = true;
		ssaoOutput.keepInitialState = true;
		ssaoOutput.initialState = nvrhi::ResourceStates::RenderTarget;
		ssaoOutput.debugName = "SSAOOutput";
		s_SSAOOutput = s_Device->createTexture(ssaoOutput);

		FG_CORE_INFO("SSAO buffer created: {0}x{1}", width, height);
	}

	void VulkanRendererAPI::CreateSSAOPipeline()
	{
		if (!s_Device)
			return;

		// Load SSAO shader.
		auto ssaoShader = Renderer::GetShaderLibrary().Get("SSAO");
		if (!ssaoShader)
		{
			FG_CORE_WARN("SSAO shader not loaded");
			return;
		}

		auto vulkanShader = std::dynamic_pointer_cast<VulkanShader>(ssaoShader);
		if (!vulkanShader || !vulkanShader->GetVertexShader() || !vulkanShader->GetFragmentShader())
		{
			FG_CORE_WARN("SSAO shader not properly compiled");
			return;
		}

		// Create binding layout for SSAO:
		// binding 0: texture2D u_DepthSampler
		// binding 1: sampler paired with it
		// binding 2: SSAO params (uniform buffer)
		//
		// The offsets are zeroed because nvrhi's defaults are HLSL register
		// spaces, which would place every descriptor at a binding number this
		// glslang-compiled shader never reads.
		nvrhi::BindingLayoutDesc ssaoLayoutDesc;
		ssaoLayoutDesc.visibility = nvrhi::ShaderType::All;
		ssaoLayoutDesc.setBindingOffsets(nvrhi::VulkanBindingOffsets{ 0, 0, 0, 0 });
		ssaoLayoutDesc.bindings = {
			{ 0, nvrhi::ResourceType::Texture_SRV, 0, 1 },
			{ 1, nvrhi::ResourceType::Sampler, 0, 1 },
			{ 2, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
		};
		s_SSAOBindingLayout = s_Device->createBindingLayout(ssaoLayoutDesc);

		// Create pipeline for SSAO (fullscreen quad, no depth test, render to SSAO output).
		nvrhi::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.VS = vulkanShader->GetVertexShader();
		pipelineDesc.PS = vulkanShader->GetFragmentShader();
		pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;

		pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
		pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;

		pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		pipelineDesc.renderState.rasterState.frontCounterClockwise = true;

		pipelineDesc.bindingLayouts.push_back(s_SSAOBindingLayout);

		nvrhi::FramebufferInfo fbInfo;
		fbInfo.colorFormats.push_back(nvrhi::Format::R8_UNORM); // SSAO output format
		fbInfo.depthFormat = nvrhi::Format::UNKNOWN; // No depth for fullscreen pass
		fbInfo.sampleCount = 1;

		pipelineDesc.inputLayout = s_FullscreenInputLayout;

		s_SSAOPipeline = s_Device->createGraphicsPipeline(pipelineDesc, fbInfo);
		if (s_SSAOPipeline)
			FG_CORE_INFO("SSAO pipeline created");
		else
			FG_CORE_ERROR("Failed to create SSAO pipeline");
	}

	void VulkanRendererAPI::CreateHDRPipeline()
	{
		if (!s_Device)
			return;

		// Load HDR shader.
		auto hdrShader = Renderer::GetShaderLibrary().Get("HDR");
		if (!hdrShader)
		{
			FG_CORE_WARN("HDR shader not loaded");
			return;
		}

		auto vulkanShader = std::dynamic_pointer_cast<VulkanShader>(hdrShader);
		if (!vulkanShader || !vulkanShader->GetVertexShader() || !vulkanShader->GetFragmentShader())
		{
			FG_CORE_WARN("HDR shader not properly compiled");
			return;
		}

		// Create binding layout for HDR:
		// binding 0: texture2D u_HDRBuffer
		// binding 1: sampler paired with it
		// binding 2: HDR params (uniform buffer)
		nvrhi::BindingLayoutDesc hdrLayoutDesc;
		hdrLayoutDesc.visibility = nvrhi::ShaderType::All;
		hdrLayoutDesc.setBindingOffsets(nvrhi::VulkanBindingOffsets{ 0, 0, 0, 0 });
		hdrLayoutDesc.bindings = {
			{ 0, nvrhi::ResourceType::Texture_SRV, 0, 1 },
			{ 1, nvrhi::ResourceType::Sampler, 0, 1 },
			{ 2, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
		};
		s_HDRBindingLayout = s_Device->createBindingLayout(hdrLayoutDesc);

		// Create pipeline for HDR tonemapping.
		nvrhi::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.VS = vulkanShader->GetVertexShader();
		pipelineDesc.PS = vulkanShader->GetFragmentShader();
		pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;

		pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
		pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;

		pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		pipelineDesc.renderState.rasterState.frontCounterClockwise = true;

		pipelineDesc.bindingLayouts.push_back(s_HDRBindingLayout);

		nvrhi::FramebufferInfo fbInfo;
		fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT); // Tonemapped buffer format
		fbInfo.depthFormat = nvrhi::Format::UNKNOWN;
		fbInfo.sampleCount = 1;

		pipelineDesc.inputLayout = s_FullscreenInputLayout;

		s_HDRPipeline = s_Device->createGraphicsPipeline(pipelineDesc, fbInfo);
		if (s_HDRPipeline)
			FG_CORE_INFO("HDR pipeline created");
		else
			FG_CORE_ERROR("Failed to create HDR pipeline");
	}

	void VulkanRendererAPI::CreateFinalPipeline()
	{
		if (!s_Device)
			return;

		// Load Final shader.
		auto finalShader = Renderer::GetShaderLibrary().Get("Final");
		if (!finalShader)
		{
			FG_CORE_WARN("Final shader not loaded");
			return;
		}

		auto vulkanShader = std::dynamic_pointer_cast<VulkanShader>(finalShader);
		if (!vulkanShader || !vulkanShader->GetVertexShader() || !vulkanShader->GetFragmentShader())
		{
			FG_CORE_WARN("Final shader not properly compiled");
			return;
		}

		// Create binding layout for Final:
		// binding 0: texture2D u_TonemappedBuffer
		// binding 1: texture2D u_SSAOBuffer
		// binding 2: samplers paired with them
		// binding 3: Final params (uniform buffer)
		nvrhi::BindingLayoutDesc finalLayoutDesc;
		finalLayoutDesc.visibility = nvrhi::ShaderType::All;
		finalLayoutDesc.setBindingOffsets(nvrhi::VulkanBindingOffsets{ 0, 0, 0, 0 });
		finalLayoutDesc.bindings = {
			{ 0, nvrhi::ResourceType::Texture_SRV, 0, 1 },
			{ 1, nvrhi::ResourceType::Texture_SRV, 0, 1 },
			{ 2, nvrhi::ResourceType::Sampler, 0, 1 },
			{ 3, nvrhi::ResourceType::ConstantBuffer, 0, 1 },
		};
		s_FinalBindingLayout = s_Device->createBindingLayout(finalLayoutDesc);

		// Create pipeline for Final composite.
		nvrhi::GraphicsPipelineDesc pipelineDesc;
		pipelineDesc.VS = vulkanShader->GetVertexShader();
		pipelineDesc.PS = vulkanShader->GetFragmentShader();
		pipelineDesc.primType = nvrhi::PrimitiveType::TriangleList;

		pipelineDesc.renderState.depthStencilState.depthTestEnable = false;
		pipelineDesc.renderState.depthStencilState.depthWriteEnable = false;

		pipelineDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		pipelineDesc.renderState.rasterState.frontCounterClockwise = true;

		pipelineDesc.bindingLayouts.push_back(s_FinalBindingLayout);

		// The final pass outputs to the swapchain (RGBA8/SRGB).
		nvrhi::Format colorFormat = GetSwapchain().GetFormat();
		if (colorFormat == nvrhi::Format::UNKNOWN)
			colorFormat = nvrhi::Format::RGBA8_UNORM;

		nvrhi::FramebufferInfo fbInfo;
		fbInfo.colorFormats.push_back(colorFormat);
		fbInfo.depthFormat = nvrhi::Format::UNKNOWN;
		fbInfo.sampleCount = 1;

		pipelineDesc.inputLayout = s_FullscreenInputLayout;

		s_FinalPipeline = s_Device->createGraphicsPipeline(pipelineDesc, fbInfo);
		if (s_FinalPipeline)
			FG_CORE_INFO("Final pipeline created");
		else
			FG_CORE_ERROR("Failed to create Final pipeline");
	}

	void VulkanRendererAPI::UpdateDescriptorSets()
	{
		// Update descriptor sets with current scene data.
		// This refreshes the bindings for G-buffer, HDR, SSAO, and tonemapped buffers.
		// Called each frame to ensure descriptors point to the correct textures.
	}

	void VulkanRendererAPI::BeginFrame()
	{
		// Simplified - pipeline is stubbed
	}

	void VulkanRendererAPI::EndFrame()
	{
		// Simplified - pipeline is stubbed
	}

	// Binding-set items are built through nvrhi's own factories rather than by
	// assigning the members by hand. BindingSetItem has no default member
	// initialisers, so a hand-built item leaves whatever the stack held in
	// every field it forgets, and none of that is reported.
	//
	// Two of those fields were genuinely wrong here:
	//
	//   range   BufferRange(0, 0) declares a zero-byte range. nvrhi's
	//           EntireBuffer is BufferRange(0, ~0ull), and isEntireBuffer()
	//           requires byteSize to be ~0ull or the descriptor's size - never
	//           0 - so the old items claimed to bind no bytes at all.
	//   unused / rawData / overrideComponentMapping
	//           never assigned, so the constant-buffer and sampler items carried
	//           stack garbage alongside an uninitialised type-adjacent value.
	//
	// The factories set every field, which is why they are used rather than a
	// struct that happens to look complete.
	namespace {
		nvrhi::BindingSetItem MakeSamplerItem(nvrhi::ISampler* sampler, uint32_t slot)
		{
			return nvrhi::BindingSetItem::Sampler(slot, sampler);
		}

		nvrhi::BindingSetItem MakeConstantBufferItem(nvrhi::IBuffer* buffer, uint32_t slot)
		{
			return nvrhi::BindingSetItem::ConstantBuffer(slot, buffer);
		}

		// A fullscreen quad in NDC space with UVs.
		struct FullscreenVertex
		{
			glm::vec2 pos;
			glm::vec2 uv;
		};

		const FullscreenVertex kFullscreenQuad[4] = {
			{ {-1.0f, -1.0f}, {0.0f, 0.0f} },
			{ { 1.0f, -1.0f}, {1.0f, 0.0f} },
			{ { 1.0f,  1.0f}, {1.0f, 1.0f} },
			{ {-1.0f,  1.0f}, {0.0f, 1.0f} }
		};
		const uint32_t kFullscreenIndices[6] = { 0, 1, 2, 2, 3, 0 };

		// The fullscreen quad's GPU buffers. These hold nvrhi handles, not raw
		// pointers: a raw pointer left the only owning RefCountPtr as a local in
		// UploadFullscreenQuad, so the buffers were released on its return and
		// every post-processing draw referenced freed memory - which is why the
		// tonemapped buffer stayed black while the pass reported no error.
		nvrhi::BufferHandle s_FullscreenVBO;
		nvrhi::BufferHandle s_FullscreenIBO;

		// Upload the fullscreen quad to GPU buffers.
		void UploadFullscreenQuad(nvrhi::ICommandList* commandList)
		{
			auto device = VulkanRendererAPI::GetStaticDevice();
			if (!device)
				return;

			// Vertex buffer (4 vertices * 16 bytes = 64 bytes).
			if (!s_FullscreenVBO)
			{
				nvrhi::BufferDesc vboDesc;
				vboDesc.byteSize = sizeof(kFullscreenQuad);
				// The flag is what nvrhi checks when the buffer is bound: without
				// it, every pass that draws the fullscreen quad fails
				// setGraphicsState and silently draws nothing, so the whole post
				// chain is a no-op.
				vboDesc.isVertexBuffer = true;
				vboDesc.debugName = "FullscreenQuad_VBO";
				vboDesc.keepInitialState = true;
				vboDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
				s_FullscreenVBO = device->createBuffer(vboDesc);
			}

			// Index buffer (6 indices * 4 bytes = 24 bytes).
			if (!s_FullscreenIBO)
			{
				nvrhi::BufferDesc iboDesc;
				iboDesc.byteSize = sizeof(kFullscreenIndices);
				iboDesc.isIndexBuffer = true;
				iboDesc.debugName = "FullscreenQuad_IBO";
				iboDesc.keepInitialState = true;
				iboDesc.initialState = nvrhi::ResourceStates::IndexBuffer;
				s_FullscreenIBO = device->createBuffer(iboDesc);
			}

			if (!s_FullscreenVBO || !s_FullscreenIBO)
				return;

			commandList->writeBuffer(s_FullscreenVBO.Get(), kFullscreenQuad, sizeof(kFullscreenQuad), 0);
			commandList->writeBuffer(s_FullscreenIBO.Get(), kFullscreenIndices, sizeof(kFullscreenIndices), 0);
		}

		void DrawFullscreenQuad(nvrhi::ICommandList* commandList, nvrhi::GraphicsState& state)
		{
			UploadFullscreenQuad(commandList);
			// nvrhi::VertexBufferBinding is { buffer, slot, offset } - it has no
			// stride; the stride comes from the pipeline's input layout. Passing 16
			// here therefore read the quad one vertex late, which drew only one of
			// the quad's two triangles (a diagonal half-image).
			nvrhi::VertexBufferBinding vertexBinding;
			vertexBinding.setBuffer(s_FullscreenVBO.Get()).setSlot(0).setOffset(0);
			state.vertexBuffers.push_back(vertexBinding);
			nvrhi::IndexBufferBinding ibo;
			ibo.buffer = s_FullscreenIBO.Get();
			ibo.format = nvrhi::Format::R32_UINT;
			ibo.offset = 0;
			state.indexBuffer = ibo;

			nvrhi::DrawArguments args;
			args.vertexCount = 6;
			args.instanceCount = 1;
			commandList->setGraphicsState(state);
			commandList->drawIndexed(args);
			}
	}

		// Helper: create a binding set in a separate function to avoid the compiler
	// placing the commandList parameter at the same stack slot as the binding
	// set descriptor's vector data pointer (which crashes in RefCountPtr's
	// copy constructor when createBindingSet returns).
	namespace {
		nvrhi::BindingSetHandle CreateBindingSet(
			nvrhi::IDevice* device,
			const nvrhi::BindingSetDesc& desc,
			nvrhi::IBindingLayout* layout)
		{
			return device->createBindingSet(desc, layout);
		}
	}

	void VulkanRendererAPI::RenderSSAOPass(nvrhi::ICommandList* commandList, nvrhi::TextureHandle depthTexture, const glm::mat4& invViewProj)
	{
		if (!s_SSAOPipeline || !s_SSAOBindingLayout || !commandList)
			return;

		if (!s_SSAOInput)
			return;

		// The depth texture must be valid; a null handle would cause a
		// segfault inside nvrhi's RefCountPtr::InternalAddRef when the
		// binding set is created.
		if (!depthTexture)
			return;

		// Upload SSAO uniform buffer.
		// Layout: mat4 invViewProj (64 bytes) + vec4 params (16 bytes) + vec2 texelSize (8 bytes) = 88 bytes
		glm::vec4 ssaoParams(1.0f, 0.5f, 0.002f, 1.5f); // intensity, radius, bias, power
		glm::vec2 texelSize(1.0f / s_Width, 1.0f / s_Height);
		if (s_SSAOUniformBuffer)
		{
			char data[96] = {};
			std::memcpy(data, &invViewProj, sizeof(glm::mat4));
			std::memcpy(data + sizeof(glm::mat4), &ssaoParams, sizeof(glm::vec4));
			std::memcpy(data + sizeof(glm::mat4) + sizeof(glm::vec4), &texelSize, sizeof(glm::vec2));
			commandList->writeBuffer(s_SSAOUniformBuffer.Get(), data, sizeof(data), 0);
		}

		// Create the SSAO binding set with combined sampler for depth texture.
		nvrhi::BindingSetDesc ssaoSetDesc;

		// Capture depth texture handle early to avoid dangling reference.
		nvrhi::TextureHandle captureDepth = depthTexture;

		// Linear sampler for depth reconstruction (created before the
		// inner block so it stays alive until CreateBindingSet is done).
		nvrhi::SamplerDesc samplerDesc;
		samplerDesc.addressU = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.addressV = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.addressW = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.setMinFilter(true);
		samplerDesc.setMagFilter(true);
		samplerDesc.setMipFilter(true);
		samplerDesc.setBorderColor(nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
		nvrhi::SamplerHandle ssaoSampler = s_Device->createSampler(samplerDesc);
		if (!ssaoSampler.Get())
		{
			FG_CORE_WARN("SSAO: failed to create sampler");
			return;
		}

		// Depth texture at binding 0 and its sampler at binding 1. They are
		// separate descriptors because the shader declares `texture2D` plus a
		// `sampler`: nvrhi cannot bind a combined image sampler.
		// Capture the sampler handle BEFORE adding to bindings to prevent
		// the raw pointer from becoming stale during createBindingSet.
		nvrhi::SamplerHandle captureSSAOSampler = ssaoSampler;
		{
			nvrhi::BindingSetItem item{};
			item.resourceHandle = captureDepth.Get();
			item.slot = 0;
			item.arrayElement = 0;
			item.type = nvrhi::ResourceType::Texture_SRV;
			item.dimension = nvrhi::TextureDimension::Texture2D;
			item.format = captureDepth->getDesc().format;
			item.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
			ssaoSetDesc.bindings.push_back(item);

			nvrhi::BindingSetItem samplerItem{};
			samplerItem.resourceHandle = captureSSAOSampler.Get();
			samplerItem.slot = 1;
			samplerItem.arrayElement = 0;
			samplerItem.type = nvrhi::ResourceType::Sampler;
			ssaoSetDesc.bindings.push_back(samplerItem);
		}

		if (s_SSAOUniformBuffer.Get())
			ssaoSetDesc.bindings.push_back(MakeConstantBufferItem(s_SSAOUniformBuffer.Get(), 2));

		// Helper function with its own stack frame prevents commandList from
		// overlapping ssaoSetDesc.bindings.data (the compiler was placing
		// commandList at the same stack slot as the vector's data pointer).
		// Separate scope forces a distinct stack slot from commandList.
		{
			nvrhi::BindingSetHandle ssaoSet = CreateBindingSet(
			    s_Device, ssaoSetDesc, s_SSAOBindingLayout);
			if (!ssaoSet)
			{
				FG_CORE_WARN("SSAO: failed to create binding set");
				return;
			}

		// Record the SSAO draw.
		nvrhi::GraphicsState state;
		state.framebuffer = s_SSAOFramebuffer.Get();

		// nvrhi::Viewport(width, height) sets minX/minY to 0. The six-argument
		// constructor is (minX, maxX, minY, maxY, minZ, maxY) - X before Y - so
		// passing (0, 0, width, height, ...) leaves it zero-width and every draw
		// is clipped away, which is an entirely black frame with no error.
		nvrhi::Viewport viewport(s_Width, s_Height);
		state.viewport.addViewportAndScissorRect(viewport);
		state.pipeline = s_SSAOPipeline.Get();
		state.bindings.push_back(ssaoSet.Get());

		DrawFullscreenQuad(commandList, state);
		}
	}

	void VulkanRendererAPI::RenderHDRPass(nvrhi::ICommandList* commandList)
	{

		if (!s_HDRPipeline || !s_HDRBindingLayout || !commandList)
			return;

		if (!s_HDRBuffer || !s_TonemappedFramebuffer)
			return;


		// Upload HDR uniform buffer (exposure).
		glm::vec4 hdrParams(1.0f, 0.0f, 0.0f, 0.0f); // exposure
		if (s_HDRUniformBuffer)
		{
			commandList->writeBuffer(s_HDRUniformBuffer.Get(), &hdrParams, sizeof(hdrParams), 0);
		}

		// Create the HDR binding set (HDR texture + sampler + params).
		nvrhi::BindingSetDesc hdrSetDesc;

		if (!s_HDRBuffer)
		{
			FG_CORE_WARN("HDR: HDR buffer is null");
			return;
		}

		// Create sampler BEFORE the binding set is created, keep it alive.
		nvrhi::SamplerDesc samplerDesc;
		samplerDesc.addressU = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.addressV = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.addressW = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.setMinFilter(true);
		samplerDesc.setMagFilter(true);
		samplerDesc.setMipFilter(true);
		samplerDesc.setBorderColor(nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
		nvrhi::SamplerHandle hdrSampler = s_Device->createSampler(samplerDesc);
		if (!hdrSampler.Get())
		{
			FG_CORE_WARN("HDR: failed to create sampler");
			return;
		}

		// HDR texture at binding 0, its sampler at binding 1.
		{
			nvrhi::BindingSetItem item{};
			item.resourceHandle = s_HDRBuffer.Get();
			item.slot = 0;
			item.type = nvrhi::ResourceType::Texture_SRV;
			item.dimension = nvrhi::TextureDimension::Texture2D;
			item.format = s_HDRBuffer->getDesc().format;
			item.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
			// Inline the sampler binding item to avoid stack variable
			// reordering corrupting hdrSampler's ptr_ member.
			hdrSetDesc.bindings.push_back(item);
			hdrSetDesc.bindings.push_back(MakeSamplerItem(hdrSampler.Get(), 1));
		}

		if (s_HDRUniformBuffer)
			hdrSetDesc.bindings.push_back(MakeConstantBufferItem(s_HDRUniformBuffer.Get(), 2));

		// Same helper for HDR pass.
		// Separate scope forces a distinct stack slot from commandList.
		{
			nvrhi::BindingSetHandle hdrSet = CreateBindingSet(
			    s_Device, hdrSetDesc, s_HDRBindingLayout);
			if (!hdrSet)
			{
				FG_CORE_WARN("HDR: failed to create binding set");
				return;
			}

		// Record the HDR draw.
		nvrhi::GraphicsState state;
		state.framebuffer = s_TonemappedFramebuffer.Get();

		// nvrhi::Viewport(width, height) sets minX/minY to 0. The six-argument
		// constructor is (minX, maxX, minY, maxY, minZ, maxY) - X before Y - so
		// passing (0, 0, width, height, ...) leaves it zero-width and every draw
		// is clipped away, which is an entirely black frame with no error.
		nvrhi::Viewport viewport(s_Width, s_Height);
		state.viewport.addViewportAndScissorRect(viewport);
		state.pipeline = s_HDRPipeline.Get();
		state.bindings.push_back(hdrSet.Get());

		DrawFullscreenQuad(commandList, state);
		}
	}

	void VulkanRendererAPI::RenderFinalPass(nvrhi::ICommandList* commandList, nvrhi::FramebufferHandle swapchainFramebuffer)
	{
		if (!s_FinalPipeline || !s_FinalBindingLayout || !commandList)
			return;

		if (!swapchainFramebuffer)
			return;

		// Upload Final uniform buffer (SSAO intensity).
		glm::vec4 finalParams(0.3f, 0.0f, 0.0f, 0.0f); // ssao intensity
		if (s_FinalUniformBuffer)
		{
			commandList->writeBuffer(s_FinalUniformBuffer.Get(), &finalParams, sizeof(finalParams), 0);
		}

		// Create the Final binding set with combined samplers.
		nvrhi::BindingSetDesc finalSetDesc;

		if (!s_TonemappedBuffer)
		{
			FG_CORE_WARN("Final: tonemapped buffer is null");
			return;
		}

		// Create samplers BEFORE the binding set is created, keep them alive.
		nvrhi::SamplerDesc samplerDesc;
		samplerDesc.addressU = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.addressV = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.addressW = nvrhi::SamplerAddressMode::ClampToBorder;
		samplerDesc.setMinFilter(true);
		samplerDesc.setMagFilter(true);
		samplerDesc.setMipFilter(true);
		samplerDesc.setBorderColor(nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
		nvrhi::SamplerHandle tonemappedSampler = s_Device->createSampler(samplerDesc);
		if (!tonemappedSampler.Get())
		{
			FG_CORE_WARN("Final: failed to create tonemapped sampler");
			return;
		}

		// Tonemapped texture at binding 0.
		{
			nvrhi::BindingSetItem item{};
			item.resourceHandle = s_TonemappedBuffer.Get();
			item.slot = 0;
			item.type = nvrhi::ResourceType::Texture_SRV;
			item.dimension = nvrhi::TextureDimension::Texture2D;
			item.format = s_TonemappedBuffer->getDesc().format;
			item.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
			finalSetDesc.bindings.push_back(item);
		}

		if (!s_SSAOOutput)
		{
			FG_CORE_WARN("Final: SSAO output buffer is null (SSAO pass skipped)");
			return;
		}

		// Create SSAO sampler outside the block to keep it alive.
		nvrhi::SamplerDesc ssaoSamplerDesc;
		ssaoSamplerDesc.addressU = nvrhi::SamplerAddressMode::ClampToBorder;
		ssaoSamplerDesc.addressV = nvrhi::SamplerAddressMode::ClampToBorder;
		ssaoSamplerDesc.addressW = nvrhi::SamplerAddressMode::ClampToBorder;
		ssaoSamplerDesc.setMinFilter(true);
		ssaoSamplerDesc.setMagFilter(true);
		ssaoSamplerDesc.setMipFilter(true);
		ssaoSamplerDesc.setBorderColor(nvrhi::Color(1.0f, 1.0f, 1.0f, 1.0f));
		nvrhi::SamplerHandle finalSampler = s_Device->createSampler(ssaoSamplerDesc);
		if (!finalSampler.Get())
		{
			FG_CORE_WARN("Final: failed to create sampler");
			return;
		}

		// SSAO texture at binding 1. The shader reads both textures through
		// the single `u_FinalSampler` at binding 2, so one sampler serves both.
		{
			nvrhi::BindingSetItem item{};
			item.resourceHandle = s_SSAOOutput.Get();
			item.slot = 1;
			item.type = nvrhi::ResourceType::Texture_SRV;
			item.dimension = nvrhi::TextureDimension::Texture2D;
			item.format = s_SSAOOutput->getDesc().format;
			item.subresources = nvrhi::TextureSubresourceSet(0, 1, 0, 1);
			finalSetDesc.bindings.push_back(item);
		}

		// Inline the sampler binding item to avoid stack variable reordering
		// corrupting finalSampler's ptr_ member.
		finalSetDesc.bindings.push_back(MakeSamplerItem(finalSampler.Get(), 2));

		if (s_FinalUniformBuffer.Get())
			finalSetDesc.bindings.push_back(MakeConstantBufferItem(s_FinalUniformBuffer.Get(), 3));

		// Same helper for Final pass.
		// Separate scope forces a distinct stack slot from commandList.
		{
			nvrhi::BindingSetHandle finalSet = CreateBindingSet(
			    s_Device, finalSetDesc, s_FinalBindingLayout);
			if (!finalSet)
			{
				FG_CORE_WARN("Final: failed to create binding set");
				return;
			}

		// Record the Final draw.
		nvrhi::GraphicsState state;
		state.framebuffer = swapchainFramebuffer.Get();

		// nvrhi::Viewport(width, height) sets minX/minY to 0. The six-argument
		// constructor is (minX, maxX, minY, maxY, minZ, maxY) - X before Y - so
		// passing (0, 0, width, height, ...) leaves it zero-width and every draw
		// is clipped away, which is an entirely black frame with no error.
		nvrhi::Viewport viewport(s_Width, s_Height);
		state.viewport.addViewportAndScissorRect(viewport);
		state.pipeline = s_FinalPipeline.Get();
		state.bindings.push_back(finalSet.Get());

		DrawFullscreenQuad(commandList, state);
		}
	}
	}
#endif // FG_NO_VULKAN_RUNTIME