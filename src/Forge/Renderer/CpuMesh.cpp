#include "fgpch.h"
#include "Forge/Renderer/CpuMesh.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/material.h>

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

	namespace MeshImport {

		namespace {

			// assimp stores a 4x3 matrix, column-major from GLM's point of view.
			glm::mat4 ToGlm(const aiMatrix4x4& matrix)
			{
				glm::mat4 result(1.0f);
				for (unsigned int c = 0; c < 4; ++c)
					for (unsigned int r = 0; r < 3; ++r)
						result[c][r] = matrix[r][c];
				return result;
			}

			// The transform of the node that owns a mesh, accumulated down from
			// the root. A glTF file puts every mesh under a node with a position,
			// rotation and scale of its own; ignoring that stacks every part of a
			// model at the origin.
			glm::mat4 NodeTransform(const aiNode* node, const aiNode* root)
			{
				glm::mat4 transform(1.0f);
				const aiNode* current = node;
				while (current)
				{
					transform = ToGlm(current->mTransformation) * transform;
					if (current == root)
						break;
					current = current->mParent;
				}
				return transform;
			}

			// Finds the node that instantiates a given mesh, so its transform can
			// be applied. Returns the root when the file has no such node.
			const aiNode* FindMeshNode(const aiNode* node, unsigned int meshIndex, const aiNode* root)
			{
				for (unsigned int i = 0; i < node->mNumMeshes; ++i)
				{
					if (node->mMeshes[i] == meshIndex)
						return node;
				}

				for (unsigned int i = 0; i < node->mNumChildren; ++i)
				{
					if (const aiNode* found = FindMeshNode(node->mChildren[i], meshIndex, root))
						return found;
				}

				return nullptr;
			}

			std::string ResolveTexturePath(const std::string& modelPath, const aiString& texture)
			{
				if (texture.length == 0)
					return {};

				const std::string directory = modelPath.substr(0, modelPath.find_last_of("/\\") + 1);
				return directory + texture.C_Str();
			}

			// Fills in the material fields of `out` from the model file's
			// material. Every read is optional: a material that declares only a
			// base colour is normal, and a file that declares none is normal too.
			void ReadMaterial(const aiMaterial* material, const std::string& modelPath, ImportedModel& out)
			{
				if (!material)
					return;

				aiColor3D colour(0.0f, 0.0f, 0.0f);
				if (material->Get(AI_MATKEY_COLOR_DIFFUSE, colour) == AI_SUCCESS)
					out.Albedo = glm::vec3(colour.r, colour.g, colour.b);

				if (material->Get(AI_MATKEY_METALLIC_FACTOR, out.Metallic) != AI_SUCCESS)
					out.Metallic = 0.0f;
				if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, out.Roughness) != AI_SUCCESS)
					out.Roughness = 0.5f;

				// glTF has no ambient occlusion term; its authors write the
				// occlusion into glossiness (which is 1 - roughness), and the
				// importer had been reading it as shininess and dividing by 255 -
				// a value that is almost always 0, so every imported model came
				// out fully occluded.
				float occlusion = 1.0f;
				if (material->Get(AI_MATKEY_GLOSSINESS_FACTOR, occlusion) == AI_SUCCESS)
					out.AO = glm::clamp(occlusion, 0.0f, 1.0f);

				aiString path;
				if (material->GetTexture(aiTextureType_DIFFUSE, 0, &path) == AI_SUCCESS)
					out.AlbedoTexturePath = ResolveTexturePath(modelPath, path);
				if (material->GetTexture(aiTextureType_NORMALS, 0, &path) == AI_SUCCESS)
					out.NormalTexturePath = ResolveTexturePath(modelPath, path);
				if (material->GetTexture(aiTextureType_METALNESS, 0, &path) == AI_SUCCESS)
					out.MetallicRoughnessTexturePath = ResolveTexturePath(modelPath, path);
			}

		}  // namespace

		bool ImportModel(const std::string& filepath, ImportedModel& out)
		{
			out = ImportedModel{};

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
				return false;
			}

			bool materialRead = false;

			for (unsigned int i = 0; i < scene->mNumMeshes; i++)
			{
				const aiMesh* mesh = scene->mMeshes[i];
				if (!mesh)
					continue;

				// Every mesh's indices start at zero, so the vertex they index
				// into has to be offset by the vertices already appended. Without
				// it a file with two meshes indexes the first mesh's vertices with
				// the second mesh's offsets - or past the end of the buffer.
				const uint32_t baseVertex = static_cast<uint32_t>(out.Vertices.size());

				const aiNode* owner = FindMeshNode(scene->mRootNode, i, scene->mRootNode);
				const glm::mat4 transform = owner ? NodeTransform(owner, scene->mRootNode) : glm::mat4(1.0f);
				const glm::mat3 normalTransform = glm::mat3(glm::transpose(glm::inverse(transform)));

				for (unsigned int j = 0; j < mesh->mNumVertices; j++)
				{
					Vertex vertex;
					vertex.Position = glm::vec3(transform * glm::vec4(
						mesh->mVertices[j].x, mesh->mVertices[j].y, mesh->mVertices[j].z, 1.0f));

					const glm::vec3 normal(
						mesh->mNormals ? mesh->mNormals[j].x : 0.0f,
						mesh->mNormals ? mesh->mNormals[j].y : 1.0f,
						mesh->mNormals ? mesh->mNormals[j].z : 0.0f);
					vertex.Normal = glm::normalize(normalTransform * normal);

					vertex.UV = mesh->mTextureCoords[0]
						? glm::vec2(mesh->mTextureCoords[0][j].x, mesh->mTextureCoords[0][j].y)
						: glm::vec2(0.0f);
					vertex.Tangent = mesh->mTangents
						? glm::vec3(mesh->mTangents[j].x, mesh->mTangents[j].y, mesh->mTangents[j].z)
						: glm::vec3(1.0f, 0.0f, 0.0f);
					vertex.Bitangent = mesh->mBitangents
						? glm::vec3(mesh->mBitangents[j].x, mesh->mBitangents[j].y, mesh->mBitangents[j].z)
						: glm::vec3(0.0f, 0.0f, 1.0f);
					out.Vertices.push_back(vertex);
				}

				for (unsigned int j = 0; j < mesh->mNumFaces; j++)
				{
					const aiFace& face = mesh->mFaces[j];
					for (unsigned int k = 0; k < face.mNumIndices; k++)
						out.Indices.push_back(baseVertex + face.mIndices[k]);
				}

				// The material is taken from the first mesh that declares one:
				// the renderer draws one material per entity, and a model whose
				// parts disagree is better approximated by one of them than by
				// none. A multi-material model is a bigger change than this.
				//
				// "First" is load-bearing: this used to be guarded on the texture
				// path being empty, which is true for a material with no texture
				// and so let *every* mesh write, leaving the last mesh's material
				// on a model the author had ordered the other way round.
				if (!materialRead && mesh->mMaterialIndex != static_cast<unsigned int>(-1)
					&& mesh->mMaterialIndex < scene->mNumMaterials)
				{
					ReadMaterial(scene->mMaterials[mesh->mMaterialIndex], filepath, out);
					materialRead = true;
				}
			}

			if (out.Vertices.empty())
			{
				FG_CORE_ERROR("Imported mesh has no vertices: {0}", filepath);
				return false;
			}

			// An index outside the vertex buffer is not a rendering bug, it is
			// undefined behaviour on the GPU. The importer is where that can be
			// proven impossible.
			for (uint32_t index : out.Indices)
			{
				if (index >= out.Vertices.size())
				{
					FG_CORE_ERROR("Imported mesh '{0}' has an index ({1}) past its vertex buffer ({2})",
						filepath, index, out.Vertices.size());
					return false;
				}
			}

			return true;
		}

	}  // namespace MeshImport

	Ref<CpuMesh> CpuMesh::LoadFromFile(const std::string& filepath)
	{
		ImportedModel model;
		if (!MeshImport::ImportModel(filepath, model))
			return nullptr;

		return CreateRef<CpuMesh>(std::move(model.Vertices), std::move(model.Indices));
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