#include "fgpch.h"
#include "Forge/Renderer/Mesh.h"

#include "Platform/Vulkan/VulkanMesh.h"

namespace Forge {

	Ref<Mesh> Mesh::Create(const std::string& filepath)
	{
		return VulkanMesh::Create(filepath);
	}

}
