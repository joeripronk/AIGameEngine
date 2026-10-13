#include "fgpch.h"
#include "Forge/Renderer/RenderCommand.h"

namespace Forge {

	Scope<RendererAPI> RenderCommand::s_RendererAPI = RendererAPI::Create();

}
