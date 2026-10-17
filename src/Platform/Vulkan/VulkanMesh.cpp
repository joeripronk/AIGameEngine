#include "fgpch.h"
#include "Platform/Vulkan/VulkanMesh.h"
#include "Platform/Vulkan/VulkanVertexArray.h"
#include "Platform/Vulkan/VulkanVertexBuffer.h"
#include "Platform/Vulkan/VulkanIndexBuffer.h"

#include "Forge/Renderer/Renderer.h"
#include "Forge/Renderer/CpuMesh.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	VulkanMesh::VulkanMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const Material& material)
		: m_Vertices(vertices), m_Indices(indices), m_IndexCount((uint32_t)indices.size()), m_Material(material)
	{
		// Create vertex buffer
		auto vb = VulkanVertexBuffer::Create(sizeof(Vertex) * vertices.size(), vertices.data());
		vb->SetLayout({ 3, 3, 2, 3, 3 }); // Position, Normal, UV, Tangent, Bitangent

		// Create index buffer
		auto ib = VulkanIndexBuffer::Create((uint32_t)indices.size(), indices.data());

		// Create vertex array
		m_VertexArray = CreateRef<VulkanVertexArray>(std::vector<Ref<VertexBuffer>>{ vb }, ib);
	}

	VulkanMesh::~VulkanMesh()
	{
	}

	void VulkanMesh::Bind() const
	{
		m_VertexArray->Bind();
	}

	void VulkanMesh::Unbind() const
	{
		m_VertexArray->Unbind();
	}

	Ref<Mesh> Mesh::CreateFromCpu(CpuMesh* cpuMesh, const Material& material)
	{
		if (!cpuMesh)
		{
			FG_CORE_ERROR("Mesh::CreateFromCpu: null CPU mesh");
			return nullptr;
		}

		const auto& vertices = cpuMesh->GetVertices();
		const auto& indices = cpuMesh->GetIndices();

		if (vertices.empty() || indices.empty())
		{
			FG_CORE_ERROR("Mesh::CreateFromCpu: CPU mesh has no geometry");
			return nullptr;
		}

		return CreateRef<VulkanMesh>(vertices, indices, material);
	}

	Ref<Mesh> Mesh::Create(const std::string& filepath)
	{
		// One importer, shared with the headless path (CpuMesh::LoadFromFile).
		// This used to carry its own copy of the assimp walk, and the two copies
		// disagreed: this one omitted the base vertex every mesh's indices need,
		// so a file with two meshes indexed past the end of the vertex buffer,
		// and ignored the node transforms that place the parts of a model.
		ImportedModel model;
		if (!MeshImport::ImportModel(filepath, model))
			return nullptr;

		Material material;
		material.Albedo = model.Albedo;
		material.Metallic = model.Metallic;
		material.Roughness = model.Roughness;
		material.AO = model.AO;

		// A model file names its textures; the importer reports the paths
		// resolved beside it. A texture that fails to load is reported by Texture
		// itself, and the material keeps working without it.
		if (!model.AlbedoTexturePath.empty())
			material.AlbedoTexture = Texture::Create(model.AlbedoTexturePath);
		if (!model.NormalTexturePath.empty())
			material.NormalTexture = Texture::Create(model.NormalTexturePath);
		if (!model.MetallicRoughnessTexturePath.empty())
			material.MetallicRoughnessTexture = Texture::Create(model.MetallicRoughnessTexturePath);

		return CreateRef<VulkanMesh>(model.Vertices, model.Indices, material);
	}

}

#endif // FG_NO_VULKAN_RUNTIME
