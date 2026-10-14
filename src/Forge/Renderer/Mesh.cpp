#include "fgpch.h"
#include "Forge/Renderer/Mesh.h"

#include "Forge/Renderer/CpuMesh.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Platform/Vulkan/VulkanMesh.h"

namespace Forge {

	// NOTE: The file-backed Mesh::Create factory lives in the Vulkan platform
	// layer (VulkanMesh.cpp) because it uploads to the GPU. The procedural
	// primitive factories (cube/plane/sphere) are available on every platform
	// via Forge::Primitives and Mesh.h.

}

#endif // FG_NO_VULKAN_RUNTIME
