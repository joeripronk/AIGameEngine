#pragma once

#include "Forge/Renderer/Mesh.h"

namespace Forge { namespace Vulkan {

// The raw-Vulkan mesh (BRUTAL rewrite, Phase 5): the CPU geometry is the
// asset, and VkRenderer uploads it to device-local buffers on first use.
// No vertex array object, no device handle - the renderer owns the GPU side.
class VkMesh final : public Mesh
{
public:
	VkMesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices, Material material);

	void Bind() const override {}
	void Unbind() const override {}

	uint32_t GetIndexCount() const override { return static_cast<uint32_t>(m_Indices.size()); }
	const Ref<class VertexArray>& GetVertexArray() const override { return m_VertexArray; }

	const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
	const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
	const Material& GetMaterial() const { return m_Material; }

private:
	std::vector<Vertex> m_Vertices;
	std::vector<uint32_t> m_Indices;
	Material m_Material;
	Ref<class VertexArray> m_VertexArray;
};

}}
