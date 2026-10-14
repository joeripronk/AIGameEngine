#include "fgpch.h"
#include "Forge/Renderer/CpuMesh.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glm/gtc/constants.hpp>
#include <cmath>

namespace Forge {

	CpuMesh::CpuMesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices)
		: m_Vertices(std::move(vertices)), m_Indices(std::move(indices))
	{
	}

	void CpuMesh::RecalculateNormals()
	{
		for (auto& vertex : m_Vertices)
			vertex.Normal = { 0.0f, 0.0f, 0.0f };

		for (size_t i = 0; i + 2 < m_Indices.size(); i += 3)
		{
			uint32_t i0 = m_Indices[i];
			uint32_t i1 = m_Indices[i + 1];
			uint32_t i2 = m_Indices[i + 2];
			if (i0 >= m_Vertices.size() || i1 >= m_Vertices.size() || i2 >= m_Vertices.size())
				continue;

			const Vertex& v0 = m_Vertices[i0];
			const Vertex& v1 = m_Vertices[i1];
			const Vertex& v2 = m_Vertices[i2];

			glm::vec3 edge1 = v1.Position - v0.Position;
			glm::vec3 edge2 = v2.Position - v0.Position;
			glm::vec3 faceNormal = glm::cross(edge1, edge2);

			m_Vertices[i0].Normal += faceNormal;
			m_Vertices[i1].Normal += faceNormal;
			m_Vertices[i2].Normal += faceNormal;
		}

		for (auto& vertex : m_Vertices)
		{
			float length = glm::length(vertex.Normal);
			if (length > 1e-6f)
				vertex.Normal = vertex.Normal / length;
		}
	}

	glm::vec3 CpuMesh::GetMin() const
	{
		if (m_Vertices.empty())
			return { 0.0f, 0.0f, 0.0f };

		glm::vec3 min = m_Vertices[0].Position;
		for (const auto& vertex : m_Vertices)
			min = glm::min(min, vertex.Position);
		return min;
	}

	glm::vec3 CpuMesh::GetMax() const
	{
		if (m_Vertices.empty())
			return { 0.0f, 0.0f, 0.0f };

		glm::vec3 max = m_Vertices[0].Position;
		for (const auto& vertex : m_Vertices)
			max = glm::max(max, vertex.Position);
		return max;
	}

	Ref<CpuMesh> CpuMesh::LoadFromFile(const std::string& filepath)
	{
		Assimp::Importer importer;
		const aiScene* scene = importer.ReadFile(filepath,
			aiProcess_Triangulate |
			aiProcess_FlipUVs |
			aiProcess_GenNormals |
			aiProcess_CalcTangentSpace |
			aiProcess_JoinIdenticalVertices |
			aiProcess_OptimizeMeshes);

		if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode)
		{
			FG_CORE_ERROR("Assimp failed to load '{0}': {1}", filepath, importer.GetErrorString());
			return nullptr;
		}

		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;

		for (uint32_t i = 0; i < scene->mNumMeshes; i++)
		{
			const aiMesh* mesh = scene->mMeshes[i];

			uint32_t baseVertex = (uint32_t)vertices.size();
			for (uint32_t j = 0; j < mesh->mNumVertices; j++)
			{
				Vertex vertex;
				vertex.Position = { mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z };
				vertex.Normal = mesh->mNormals
					? glm::vec3(mesh->mNormals[j].x, mesh->mNormals[j].y, mesh->mNormals[j].z)
					: glm::vec3(0.0f, 1.0f, 0.0f);
				vertex.UV = mesh->mTextureCoords[0]
					? glm::vec2(mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y)
					: glm::vec2(0.0f);
				vertex.Tangent = mesh->mTangents
					? glm::vec3(mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z)
					: glm::vec3(1.0f, 0.0f, 0.0f);
				vertex.Bitangent = mesh->mBitangents
					? glm::vec3(mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z)
					: glm::vec3(0.0f, 0.0f, 1.0f);
				vertices.push_back(vertex);
			}

			for (uint32_t j = 0; j < mesh->mNumFaces; j++)
			{
				const aiFace& face = mesh->mFaces[j];
				for (uint32_t k = 0; k < face.mNumIndices; k++)
					indices.push_back(baseVertex + face.mIndices[k]);
			}
		}

		if (vertices.empty())
		{
			FG_CORE_WARN("Imported mesh has no vertices: {0}", filepath);
			return nullptr;
		}

		return CreateRef<CpuMesh>(std::move(vertices), std::move(indices));
	}

	namespace Primitives {

		Ref<CpuMesh> CreateCube(float size)
		{
			const float h = size * 0.5f;

			// Each face gets its own 4 vertices so normals/UVs stay per-face.
			struct Face
			{
				glm::vec3 Normal;
				glm::vec3 Corners[4];
			};

			const Face faces[6] = {
				// +Z
				{ { 0, 0, 1 }, { { -h, -h, h }, { h, -h, h }, { h, h, h }, { -h, h, h } } },
				// -Z
				{ { 0, 0, -1 }, { { h, -h, -h }, { -h, -h, -h }, { -h, h, -h }, { h, h, -h } } },
				// +X
				{ { 1, 0, 0 }, { { h, -h, h }, { h, -h, -h }, { h, h, -h }, { h, h, h } } },
				// -X
				{ { -1, 0, 0 }, { { -h, -h, -h }, { -h, -h, h }, { -h, h, h }, { -h, h, -h } } },
				// +Y
				{ { 0, 1, 0 }, { { -h, h, h }, { h, h, h }, { h, h, -h }, { -h, h, -h } } },
				// -Y
				{ { 0, -1, 0 }, { { -h, -h, -h }, { h, -h, -h }, { h, -h, h }, { -h, -h, h } } },
			};

			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;
			vertices.reserve(24);
			indices.reserve(36);

			for (const auto& face : faces)
			{
				// Tangent is derived from the first edge of the face.
				glm::vec3 tangent = glm::normalize(face.Corners[1] - face.Corners[0]);
				glm::vec3 bitangent = glm::cross(face.Normal, tangent);

				uint32_t base = (uint32_t)vertices.size();
				const glm::vec2 uvs[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

				for (int i = 0; i < 4; i++)
				{
					Vertex vertex;
					vertex.Position = face.Corners[i];
					vertex.Normal = face.Normal;
					vertex.UV = uvs[i];
					vertex.Tangent = tangent;
					vertex.Bitangent = bitangent;
					vertices.push_back(vertex);
				}

				indices.push_back(base + 0);
				indices.push_back(base + 1);
				indices.push_back(base + 2);
				indices.push_back(base + 0);
				indices.push_back(base + 2);
				indices.push_back(base + 3);
			}

			return CreateRef<CpuMesh>(std::move(vertices), std::move(indices));
		}

		Ref<CpuMesh> CreatePlane(float size)
		{
			const float h = size * 0.5f;

			std::vector<Vertex> vertices(4);
			vertices[0] = { { -h, 0, -h }, { 0, 1, 0 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 } };
			vertices[1] = { { h, 0, -h }, { 0, 1, 0 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } };
			vertices[2] = { { h, 0, h }, { 0, 1, 0 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 1 } };
			vertices[3] = { { -h, 0, h }, { 0, 1, 0 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 } };

			// Counter-clockwise winding when viewed from +Y so the geometric
			// normal matches the authored +Y normal.
			std::vector<uint32_t> indices = { 0, 2, 1, 0, 3, 2 };
			return CreateRef<CpuMesh>(std::move(vertices), std::move(indices));
		}

		Ref<CpuMesh> CreateSphere(float radius, uint32_t slices, uint32_t stacks)
		{
			if (slices < 3 || stacks < 2)
			{
				FG_CORE_WARN("CreateSphere: degenerate slice/stack count ({0}, {1})", slices, stacks);
				return nullptr;
			}

			std::vector<Vertex> vertices;
			std::vector<uint32_t> indices;

			for (uint32_t stack = 0; stack <= stacks; stack++)
			{
				float phi = glm::pi<float>() * (float)stack / (float)stacks;
				float y = std::cos(phi);
				float r = std::sin(phi);

				for (uint32_t slice = 0; slice <= slices; slice++)
				{
					float theta = 2.0f * glm::pi<float>() * (float)slice / (float)slices;
					glm::vec3 normal = glm::normalize(glm::vec3(r * std::cos(theta), y, r * std::sin(theta)));

					Vertex vertex;
					vertex.Position = normal * radius;
					vertex.Normal = normal;
					vertex.UV = { (float)slice / (float)slices, (float)stack / (float)stacks };

					glm::vec3 tangent = glm::normalize(glm::vec3(-std::sin(theta), 0.0f, std::cos(theta)));
					vertex.Tangent = tangent;
					vertex.Bitangent = glm::cross(normal, tangent);

					vertices.push_back(vertex);
				}
			}

			for (uint32_t stack = 0; stack < stacks; stack++)
			{
				for (uint32_t slice = 0; slice < slices; slice++)
				{
					uint32_t current = stack * (slices + 1) + slice;
					uint32_t next = current + slices + 1;

					indices.push_back(current);
					indices.push_back(next);
					indices.push_back(current + 1);

					indices.push_back(current + 1);
					indices.push_back(next);
					indices.push_back(next + 1);
				}
			}

			return CreateRef<CpuMesh>(std::move(vertices), std::move(indices));
		}

	}

}