#pragma once

#include "Forge/Renderer/Mesh.h"

namespace Forge {

	class VulkanMesh : public Mesh
	{
	public:
		VulkanMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const Material& material);
		~VulkanMesh() override;

		void Bind() const override;
		void Unbind() const override;

		uint32_t GetIndexCount() const override { return m_IndexCount; }
		const Ref<class VertexArray>& GetVertexArray() const override { return m_VertexArray; }

		const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
		const Material& GetMaterial() const { return m_Material; }

	private:
		std::vector<Vertex> m_Vertices;
		std::vector<uint32_t> m_Indices;
		uint32_t m_IndexCount = 0;
		Material m_Material;
		Ref<VertexArray> m_VertexArray;
		nvrhi::TextureHandle m_AlbedoTex;
		nvrhi::TextureHandle m_MetallicRoughnessTex;
		nvrhi::TextureHandle m_AOTex;
		nvrhi::TextureHandle m_EmissiveTex;
		nvrhi::TextureHandle m_NormalTex;
	};

}
