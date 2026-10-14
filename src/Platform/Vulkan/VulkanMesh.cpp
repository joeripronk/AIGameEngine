#include "fgpch.h"
#include "Platform/Vulkan/VulkanMesh.h"
#include "Platform/Vulkan/VulkanVertexArray.h"
#include "Platform/Vulkan/VulkanVertexBuffer.h"
#include "Platform/Vulkan/VulkanIndexBuffer.h"

#include "Forge/Renderer/Renderer.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

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

	Ref<Mesh> Mesh::Create(const std::string& filepath)
	{
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(filepath,
			aiProcess_Triangulate |
			aiProcess_FlipUVs |
			aiProcess_GenNormals |
			aiProcess_GenTangentSpace |
			aiProcess_JoinIdenticalVertices |
			aiProcess_OptimizeMeshes);

		if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
		{
			FG_CORE_ERROR("Assimp failed to load: {0}", filepath);
			return nullptr;
		}

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		Material material;

		// Process all meshes in the scene
		for (uint32_t i = 0; i < scene->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[i];

			// Extract vertices
			for (uint32_t j = 0; j < mesh->mNumVertices; j++)
			{
				Vertex vertex;
				vertex.Position = glm::vec3(
					mesh->mVertices[j].x,
					mesh->mVertices[j].y,
					mesh->mVertices[j].z);

				vertex.Normal = glm::vec3(
					mesh->mNormals[j].x,
					mesh->mNormals[j].y,
					mesh->mNormals[j].z);

				if (mesh->mTextureCoords[0])
				{
					vertex.UV = glm::vec2(
						mesh->mTextureCoords[0][j].x,
						mesh->mTextureCoords[0][j].y);
				}
				else
				{
					vertex.UV = glm::vec2(0.0f, 0.0f);
				}

				if (mesh->mTangents)
				{
					vertex.Tangent = glm::vec3(
						mesh->mTangents[j].x,
						mesh->mTangents[j].y,
						mesh->mTangents[j].z);
				}
				else
				{
					vertex.Tangent = glm::vec3(0.0f, 1.0f, 0.0f);
				}

				if (mesh->mBitangents)
				{
					vertex.Bitangent = glm::vec3(
						mesh->mBitangents[j].x,
						mesh->mBitangents[j].y,
						mesh->mBitangents[j].z);
				}
				else
				{
					vertex.Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
				}

				vertices.push_back(vertex);
			}

			// Extract indices
			for (uint32_t j = 0; j < mesh->mNumFaces; j++)
			{
				aiFace& face = mesh->mFaces[j];
				FG_CORE_ASSERT(face.mNumIndices == 3, "Face must have exactly 3 indices");
				indices.push_back(face.mIndices[0]);
				indices.push_back(face.mIndices[1]);
				indices.push_back(face.mIndices[2]);
			}

			// Extract material
			if (mesh->mMaterialIndex >= 0)
			{
				aiMaterial* aiMat = scene->mMaterials[mesh->mMaterialIndex];

				aiColor3D color(0.0f, 0.0f, 0.0f);
				if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, color) == AI_SUCCESS)
				{
					material.Albedo = glm::vec3(color.r, color.g, color.b);
				}

				float metallic = 0.0f;
				if (aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS)
				{
					material.Metallic = metallic;
				}

				float roughness = 0.5f;
				if (aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS)
				{
					material.Roughness = roughness;
				}

				float ao = 1.0f;
				if (aiMat->Get(AI_MATKEY_GLOSSINESS, ao) == AI_SUCCESS)
				{
					material.AO = ao / 255.0f;
				}

				// Load textures
				aiString path;
				if (aiMat->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
				{
					std::string texPath = filepath.substr(0, filepath.find_last_of("/\\") + 1) + path.C_Str();
					material.AlbedoTexture = Texture::Create(texPath);
				}

				if (aiMat->GetTexture(aiTextureType_METALLIC, 0, &path) == AI_SUCCESS)
				{
					std::string texPath = filepath.substr(0, filepath.find_last_of("/\\") + 1) + path.C_Str();
					material.MetallicRoughnessTexture = Texture::Create(texPath);
				}

				if (aiMat->GetTexture(aiTextureType_NORMALS, 0, &path) == AI_SUCCESS)
				{
					std::string texPath = filepath.substr(0, filepath.find_last_of("/\\") + 1) + path.C_Str();
					material.NormalTexture = Texture::Create(texPath);
				}
			}
		}

		if (vertices.empty())
		{
			FG_CORE_ERROR("Mesh has no vertices: {0}", filepath);
			return nullptr;
		}

		return CreateRef<VulkanMesh>(vertices, indices, material);
	}

}
