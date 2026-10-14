#include "fgpch.h"
#include "Forge/Renderer/Mesh.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include "Platform/Vulkan/VulkanMesh.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

namespace Forge {

	Ref<Mesh> Mesh::Create(const std::string& filepath)
	{
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(filepath,
			aiProcess_Triangulate |
			aiProcess_FlipUVs |
			aiProcess_GenNormals |
			aiProcess_CalcTangentSpace |
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

		for (uint32_t i = 0; i < scene->mNumMeshes; i++)
		{
			aiMesh* mesh = scene->mMeshes[i];

			for (uint32_t j = 0; j < mesh->mNumVertices; j++)
			{
				Vertex vertex;
				vertex.Position = glm::vec3(mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z);
				vertex.Normal = glm::vec3(mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z);
				vertex.UV = mesh->mTextureCoords[0] ? glm::vec2(mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y) : glm::vec2(0.0f);
				vertex.Tangent = mesh->mTangents ? glm::vec3(mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z) : glm::vec3(0.0f, 1.0f, 0.0f);
				vertex.Bitangent = mesh->mBitangents ? glm::vec3(mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z) : glm::vec3(0.0f, 0.0f, 1.0f);
				vertices.push_back(vertex);
			}

			for (uint32_t j = 0; j < mesh->mNumFaces; j++)
			{
				indices.push_back(mesh->mFaces[j].mIndices[0]);
				indices.push_back(mesh->mFaces[j].mIndices[1]);
				indices.push_back(mesh->mFaces[j].mIndices[2]);
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

#endif // FG_NO_VULKAN_RUNTIME
