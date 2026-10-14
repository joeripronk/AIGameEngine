#include "fgpch.h"
#include "Forge/Renderer/RenderCommand.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	Scope<RendererAPI> RenderCommand::s_RendererAPI = RendererAPI::Create();

}

#endif // FG_NO_VULKAN_RUNTIME
