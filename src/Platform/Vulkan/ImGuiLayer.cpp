#include "fgpch.h"
#include "Platform/Vulkan/ImGuiLayer.h"

#include "Forge/Core/Application.h"
#include "Forge/Core/Log.h"

#ifndef FG_NO_VULKAN_RUNTIME

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"
#include "Platform/Vulkan/VulkanSwapchain.h"

#include <vulkan/vulkan.h>

#include <new>

namespace Forge {

	// The Vulkan backend keeps the init info - and the colour-attachment
	// format it points at - alive for the lifetime of the UI, so both are
	// heap-allocated as one block owned by the layer.
	struct VulkanBackendState
	{
		ImGui_ImplVulkan_InitInfo InitInfo;
		VkFormat RenderFormat;
	};

}  // namespace Forge

#endif

namespace Forge {

	ImGuiLayer::ImGuiLayer()
		: Layer("ImGuiLayer")
	{
	}

	ImGuiLayer::~ImGuiLayer()
	{
		if (m_HasContext)
			OnDetach();
	}

	void ImGuiLayer::ApplyStyle(ImGuiStyle& style)
	{
		// The overlay draws on top of the 3D scene, so an opaque window
		// background would hide the view it is annotating.
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.00f);
	}

	void ImGuiLayer::OnAttach()
	{
		if (m_HasContext)
			return;

		ImGui::CreateContext();
		m_HasContext = true;

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		ImGuiLayer::ApplyStyle(ImGui::GetStyle());

#ifndef FG_NO_VULKAN_RUNTIME
		auto* nativeWindow = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
		if (!nativeWindow)
		{
			FG_CORE_WARN("ImGuiLayer: no native window; overlay disabled");
			ImGui::DestroyContext();
			m_HasContext = false;
			return;
		}

		ImGui_ImplGlfw_InitForVulkan(nativeWindow, true);

		auto& swapchain = VulkanRendererAPI::GetSwapchain();
		if (!swapchain.IsValid())
		{
			FG_CORE_WARN("ImGuiLayer: no swapchain; overlay disabled");
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			m_HasContext = false;
			return;
		}

		// The backend keeps the colour-attachment format pointer alive for the
		// lifetime of the UI, so the state block outlives this function.
		auto* state = new (std::nothrow) VulkanBackendState();
		if (!state)
		{
			FG_CORE_ERROR("ImGuiLayer: out of memory for the Vulkan backend init info");
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			m_HasContext = false;
			return;
		}

		state->RenderFormat = swapchain.GetVkFormat();

		ImGui_ImplVulkan_InitInfo& initInfo = state->InitInfo;
		initInfo = {};
		initInfo.ApiVersion = VK_API_VERSION_1_3;
		initInfo.Instance = VulkanRendererAPI::GetInstance();
		initInfo.PhysicalDevice = VulkanRendererAPI::GetPhysicalDevice();
		initInfo.Device = VulkanRendererAPI::GetLogicalDevice();
		initInfo.QueueFamily = VulkanRendererAPI::GetGraphicsQueueIndex();
		initInfo.Queue = VulkanRendererAPI::GetGraphicsQueue();
		initInfo.PipelineCache = VK_NULL_HANDLE;
		initInfo.DescriptorPool = VK_NULL_HANDLE;
		initInfo.DescriptorPoolSize = 256;
		initInfo.Allocator = nullptr;
		initInfo.MinImageCount = swapchain.GetImageCount();
		initInfo.ImageCount = swapchain.GetImageCount();
		initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		initInfo.UseDynamicRendering = true;
		initInfo.PipelineRenderingCreateInfo = {};
		initInfo.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
		initInfo.PipelineRenderingCreateInfo.pNext = nullptr;
		initInfo.PipelineRenderingCreateInfo.viewMask = 0;
		initInfo.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
		initInfo.PipelineRenderingCreateInfo.pColorAttachmentFormats = &state->RenderFormat;

		if (!ImGui_ImplVulkan_Init(&initInfo))
		{
			FG_CORE_WARN("ImGuiLayer: Vulkan backend init failed; overlay disabled");
			delete state;
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			m_HasContext = false;
			return;
		}

		m_VulkanInitInfo = state;
		m_Ready = true;
#else
		FG_CORE_INFO("ImGuiLayer: headless build; context only, overlay inert");
#endif
	}

	void ImGuiLayer::OnDetach()
	{
		if (!m_HasContext)
			return;

#ifndef FG_NO_VULKAN_RUNTIME
		if (m_Ready)
			ImGui_ImplVulkan_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		delete static_cast<VulkanBackendState*>(m_VulkanInitInfo);
		m_VulkanInitInfo = nullptr;
#endif

		ImGui::DestroyContext();
		m_HasContext = false;
		m_Ready = false;
	}

	void ImGuiLayer::NewFrame()
	{
		if (!m_Ready)
			return;

#ifndef FG_NO_VULKAN_RUNTIME
		ImGui_ImplGlfw_NewFrame();
		ImGui_ImplVulkan_NewFrame();
#endif
		ImGui::NewFrame();
	}

	void ImGuiLayer::Render()
	{
		if (!m_Ready)
			return;

		ImGui::Render();

#ifndef FG_NO_VULKAN_RUNTIME
		const VkCommandBuffer commandBuffer = VulkanRendererAPI::GetCurrentCommandBuffer();
		if (commandBuffer != VK_NULL_HANDLE)
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
#endif
	}

}  // namespace Forge
