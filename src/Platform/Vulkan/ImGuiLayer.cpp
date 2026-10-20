#include "fgpch.h"
#include "Platform/Vulkan/ImGuiLayer.h"

#include "Forge/Core/Application.h"
#include "Forge/Core/Log.h"

#ifndef FG_NO_VULKAN_RUNTIME

#if !defined(FG_PLATFORM_ANDROID)
	#include "backends/imgui_impl_glfw.h"
#endif
#include "backends/imgui_impl_vulkan.h"
#include "Forge/Renderer/Renderer.h"

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
		AttachTo(Application::Get().GetWindow().GetNativeWindow());
	}

	void ImGuiLayer::ApplyConfigFlags(ImGuiIO& io)
	{
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		// Docking (the editor's DockSpace): panels rearrange by dragging
		// and the layout persists in the .ini. The Tetris overlay is
		// unaffected - a single window with nothing to dock onto it.
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	}

	bool ImGuiLayer::HasSavedDockLayout(const std::string& iniText)
	{
		// ImGui writes the dock tree as a [Docking] section whose every entry
		// is one dock node. Only a section header selects a section, so a
		// window that happens to be named "Docking" ("[Window][Docking]") is
		// not a layout, and the header line itself is not an entry.
		size_t pos = 0;
		bool inDocking = false;

		while (pos < iniText.size())
		{
			const size_t lineEnd = iniText.find('\n', pos);
			const std::string line = iniText.substr(pos, lineEnd == std::string::npos ? std::string::npos : lineEnd - pos);
			pos = lineEnd == std::string::npos ? iniText.size() : lineEnd + 1;

			const size_t first = line.find_first_not_of(" \t\r");
			if (first == std::string::npos)
				continue;

			if (line[first] == '[')
			{
				const size_t close = line.find(']', first + 1);
				if (close == std::string::npos)
					continue;
				inDocking = line.compare(first + 1, close - first - 1, "Docking") == 0;
				continue;
			}

			if (inDocking)
				return true;
		}

		return false;
	}

	bool ImGuiLayer::AttachTo(void* nativeWindow)
	{
		if (m_HasContext)
			return m_Ready;

		ImGui::CreateContext();
		m_HasContext = true;

		ImGuiIO& io = ImGui::GetIO();
		ImGuiLayer::ApplyConfigFlags(io);
		ImGuiLayer::ApplyStyle(ImGui::GetStyle());

#ifndef FG_NO_VULKAN_RUNTIME
	#if defined(FG_PLATFORM_ANDROID)
		// There is no GLFW on a phone, and ImGui has no Android platform
		// backend of its own. The overlay needs one - it is the platform
		// that tells ImGui how big the screen is and where the pointer is -
		// so the overlay is off on this platform and the game runs without
		// it, which is what the layer's contract says a game does when the
		// backends are not there.
		(void)nativeWindow;
		FG_CORE_INFO("ImGuiLayer: no ImGui platform backend for this platform; overlay disabled");
		ImGui::DestroyContext();
		m_HasContext = false;
		return false;
	#else
		auto* window = static_cast<GLFWwindow*>(nativeWindow);
		if (!window)
		{
			FG_CORE_WARN("ImGuiLayer: no native window; overlay disabled");
			ImGui::DestroyContext();
			m_HasContext = false;
			return false;
		}

		ImGui_ImplGlfw_InitForVulkan(window, true);

		if (!Renderer::HasSwapchain())
		{
			FG_CORE_WARN("ImGuiLayer: no swapchain; overlay disabled");
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			m_HasContext = false;
			return false;
		}

		if (!BindVulkanBackend(Renderer::GetSwapchainImageCount()))
		{
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
			m_HasContext = false;
			return false;
		}

		m_Ready = true;
		return true;
	#endif
#else
		(void)nativeWindow;
		FG_CORE_INFO("ImGuiLayer: headless build; context only, overlay inert");
		return false;
#endif
	}

#ifndef FG_NO_VULKAN_RUNTIME

	bool ImGuiLayer::BindVulkanBackend(uint32_t imageCount)
	{
		// The backend keeps the colour-attachment format pointer alive for
		// the lifetime of the UI, so the state block outlives this function.
		auto* state = new (std::nothrow) VulkanBackendState();
		if (!state)
		{
			FG_CORE_ERROR("ImGuiLayer: out of memory for the Vulkan backend init info");
			return false;
		}

		state->RenderFormat = static_cast<VkFormat>(Renderer::GetSwapchainVkFormat());

		ImGui_ImplVulkan_InitInfo& initInfo = state->InitInfo;
		initInfo = {};
		initInfo.ApiVersion = VK_API_VERSION_1_3;
		initInfo.Instance = static_cast<VkInstance>(Renderer::GetVulkanInstanceHandle());
		initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(Renderer::GetVulkanPhysicalDeviceHandle());
		initInfo.Device = static_cast<VkDevice>(Renderer::GetVulkanDeviceHandle());
		initInfo.QueueFamily = Renderer::GetVulkanGraphicsQueueIndex();
		initInfo.Queue = static_cast<VkQueue>(Renderer::GetVulkanGraphicsQueueHandle());
		initInfo.PipelineCache = VK_NULL_HANDLE;
		initInfo.DescriptorPool = VK_NULL_HANDLE;
		initInfo.DescriptorPoolSize = 256;
		initInfo.Allocator = nullptr;
		initInfo.MinImageCount = imageCount;
		initInfo.ImageCount = imageCount;
		initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		// The overlay draws into the present render pass, which the engine
		// leaves open for it: BeginFinalPass opens the pass, the overlay
		// records its draws into the frame's command buffer, and EndScene
		// closes it.
		//
		// So the overlay's pipeline has to be built for *that* pass. A
		// dynamic-rendering pipeline is not: it may only be used inside a
		// vkCmdBeginRendering scope, and asking for one here produced a
		// pipeline that could not be created at all (the device did not
		// have the dynamicRendering feature enabled, and the overlay's
		// draws would have been invalid inside the open render pass even
		// if it had).
		initInfo.RenderPass = static_cast<VkRenderPass>(Renderer::GetPresentRenderPass());
		initInfo.UseDynamicRendering = false;
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
			return false;
		}

		m_VulkanInitInfo = state;
		m_BackendImageCount = imageCount;
		return true;
	}

	void ImGuiLayer::UnbindVulkanBackend()
	{
		if (!m_VulkanInitInfo)
			return;

		ImGui_ImplVulkan_Shutdown();
		delete static_cast<VulkanBackendState*>(m_VulkanInitInfo);
		m_VulkanInitInfo = nullptr;
		m_BackendImageCount = 0;
	}

#endif // FG_NO_VULKAN_RUNTIME

	void ImGuiLayer::OnDetach()
	{
		if (!m_HasContext)
			return;

#ifndef FG_NO_VULKAN_RUNTIME
		UnbindVulkanBackend();
	#if !defined(FG_PLATFORM_ANDROID)
		ImGui_ImplGlfw_Shutdown();
	#endif
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
		// The swapchain is rebuilt when the window changes shape, and the
		// rebuilt swapchain can have a different number of images. The
		// backend sized its vertex buffers and its descriptor pool for the
		// count it was given, so it is bound again when the count changes:
		// a frame still in flight that reuses a buffer the backend has
		// already written is a torn overlay.
		const uint32_t imageCount = Renderer::GetSwapchainImageCount();
		if (imageCount != 0 && imageCount != m_BackendImageCount)
		{
			FG_CORE_INFO("ImGuiLayer: the swapchain now has {0} images (was {1}); rebinding the overlay",
				imageCount, m_BackendImageCount);
			UnbindVulkanBackend();
			if (!BindVulkanBackend(imageCount))
			{
				FG_CORE_WARN("ImGuiLayer: the overlay could not be rebound; it is off until the next resize");
				m_Ready = false;
				return;
			}
		}

	#if !defined(FG_PLATFORM_ANDROID)
		ImGui_ImplGlfw_NewFrame();
	#endif
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
		const VkCommandBuffer commandBuffer = static_cast<VkCommandBuffer>(Renderer::GetCurrentCommandBuffer());
		if (commandBuffer != VK_NULL_HANDLE)
			ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
#endif
	}

}  // namespace Forge
