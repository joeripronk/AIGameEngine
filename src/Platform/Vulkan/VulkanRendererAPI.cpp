#include "fgpch.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/common.h>

namespace Forge {

	void VulkanRendererAPI::Init()
	{
		// Initialize nvrhi Vulkan context
		nvrhi::vulkan::CreateDeviceDesc desc;
		desc.pApplicationName = "Forge Engine";
		desc.pEngineName = "Forge Engine";
		desc.pDeviceName = "Forge";
		nvrhi::vulkan::CreateDevice(desc);
	}

	void VulkanRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		// Set viewport via nvrhi
	}

	void VulkanRendererAPI::SetClearColor(const glm::vec4& color)
	{
		// Set clear color via nvrhi
	}

	void VulkanRendererAPI::Clear()
	{
		// Clear via nvrhi
	}

	void VulkanRendererAPI::DrawIndexed(const Ref<class VertexArray>& vertexArray, uint32_t indexCount)
	{
		// Draw indexed via nvrhi
	}

	void VulkanRendererAPI::SetLineWidth(float width)
	{
		// Set line width via nvrhi
	}

}
