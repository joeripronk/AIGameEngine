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
#include "fgpch.h"
#include "Platform/Vulkan/VkMesh.h"

#include "Platform/Vulkan/VkTexture.h"

#include "Forge/Renderer/CpuMesh.h"
#include "Forge/Renderer/Mesh.h"
#include "Forge/Renderer/Texture.h"

#include <utility>

namespace Forge {

	// The Mesh factories live here (Phase 5): the CPU geometry is the asset,
	// and VkRenderer uploads it to device-local buffers on first use.

	Ref<Mesh> Mesh::CreateFromCpu(CpuMesh* cpuMesh, const Material& material)
	{
		if (!cpuMesh)
		{
			FG_CORE_ERROR("Mesh::CreateFromCpu: null CPU mesh");
			return nullptr;
		}

		const std::vector<Vertex>& vertices = cpuMesh->GetVertices();
		const std::vector<uint32_t>& indices = cpuMesh->GetIndices();

		if (vertices.empty() || indices.empty())
		{
			FG_CORE_ERROR("Mesh::CreateFromCpu: CPU mesh has no geometry");
			return nullptr;
		}

		return CreateRef<Vulkan::VkMesh>(vertices, indices, material);
	}

	Ref<Mesh> Mesh::Create(const std::string& filepath)
	{
		// One importer, shared with the headless path (CpuMesh::LoadFromFile).
		// It applies each mesh's node transform and offsets each mesh's indices
		// by the vertices already appended - the two things a private copy of
		// the assimp walk kept getting wrong.
		ImportedModel model;
		if (!MeshImport::ImportModel(filepath, model))
			return nullptr;

		Material material;
		material.Albedo = model.Albedo;
		material.Metallic = model.Metallic;
		material.Roughness = model.Roughness;
		material.AO = model.AO;

		// A model file names its textures; the importer reports the paths
		// resolved beside it. A texture that fails to load is reported by
		// Texture itself, and the material keeps working without it.
		if (!model.AlbedoTexturePath.empty())
			material.AlbedoTexture = Texture::Create(model.AlbedoTexturePath);
		if (!model.NormalTexturePath.empty())
			material.NormalTexture = Texture::Create(model.NormalTexturePath);
		if (!model.MetallicRoughnessTexturePath.empty())
			material.MetallicRoughnessTexture = Texture::Create(model.MetallicRoughnessTexturePath);

		return CreateRef<Vulkan::VkMesh>(std::move(model.Vertices), std::move(model.Indices), std::move(material));
	}

}
