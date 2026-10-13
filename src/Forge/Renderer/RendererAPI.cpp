#include "fgpch.h"
#include "Forge/Renderer/RendererAPI.h"

#include "Platform/Vulkan/VulkanRendererAPI.h"

namespace Forge {

	RendererAPI::API RendererAPI::s_API = RendererAPI::API::Vulkan;

	Scope<RendererAPI> RendererAPI::Create()
	{
		switch (s_API)
		{
			case RendererAPI::API::None:    FG_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
			case RendererAPI::API::Vulkan:  return CreateScope<VulkanRendererAPI>();
		}

		FG_CORE_ASSERT(false, "Unknown RendererAPI!");
		return nullptr;
	}

}
