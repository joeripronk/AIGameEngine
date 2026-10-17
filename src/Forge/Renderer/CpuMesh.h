#pragma once

#include "Forge/Renderer/Mesh.h"

#include <vector>

namespace Forge {

	// A CPU-side mesh that stores interleaved vertex data without touching the
	// GPU. Used for procedural primitives (cube, sphere, plane) and for
	// testing geometry generation without a graphics device.
	class CpuMesh : public Mesh
	{
	public:
		CpuMesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices);

		void Bind() const override {}
		void Unbind() const override {}

		uint32_t GetIndexCount() const override { return (uint32_t)m_Indices.size(); }

		// A CPU mesh has no vertex array; the renderer uploads the data instead.
		const Ref<class VertexArray>& GetVertexArray() const override { return m_VertexArray; }

		const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

		// Recomputes smooth per-vertex normals by area-weighted face accumulation.
		void RecalculateNormals();

		// Axis-aligned bounding box of the mesh.
		glm::vec3 GetMin() const;
		glm::vec3 GetMax() const;

			// Imports a model file (glTF/GLB/OBJ/FBX/...) via assimp into CPU data.
		// Returns nullptr on failure. This is the device-independent import path
		// used by tests, the asset pipeline and content baking.
		static Ref<CpuMesh> LoadFromFile(const std::string& filepath);

	private:
		std::vector<Vertex> m_Vertices;
		std::vector<uint32_t> m_Indices;
		Ref<class VertexArray> m_VertexArray;
	};

	// What a model file contained: its geometry, and the material it declared.
	//
	// One importer, two consumers. The GPU path needs the texture objects and
	// the CPU path must not be able to (there is no device in a unit test), so
	// this reports texture *paths* rather than textures, and the platform layer
	// turns them into whatever its renderer needs.
	//
	// Texture paths are resolved relative to the model file, because that is
	// where the file that named them expects them to be - a model and its
	// textures move together.
	struct ImportedModel
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices;

		glm::vec3 Albedo = { 1.0f, 1.0f, 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float AO = 1.0f;

		std::string AlbedoTexturePath;
		std::string NormalTexturePath;
		std::string MetallicRoughnessTexturePath;
	};

	namespace MeshImport {

		// Reads a model file into `out`. Returns false and leaves `out` empty
		// when the file cannot be read or contains no geometry; every failure is
		// logged, because a model that silently imports as nothing looks exactly
		// like a model that was never asked for.
		[[nodiscard]]
		bool ImportModel(const std::string& filepath, ImportedModel& out);

	}  // namespace MeshImport

	// Procedural primitive generators (always available, GPU independent).
	namespace Primitives {

		Ref<CpuMesh> CreateCube(float size = 1.0f);
		Ref<CpuMesh> CreatePlane(float size = 1.0f);
		Ref<CpuMesh> CreateSphere(float radius = 0.5f, uint32_t slices = 16, uint32_t stacks = 16);

	}

}