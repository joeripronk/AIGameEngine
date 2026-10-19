#include "fgpch.h"
#include "Platform/Vulkan/VkMesh.h"

namespace Forge { namespace Vulkan {

VkMesh::VkMesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices, Material material)
	: m_Vertices(std::move(vertices))
	, m_Indices(std::move(indices))
	, m_Material(std::move(material))
{
}

}}
