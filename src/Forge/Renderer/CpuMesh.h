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

	// Procedural primitive generators (always available, GPU independent).
	namespace Primitives {

		Ref<CpuMesh> CreateCube(float size = 1.0f);
		Ref<CpuMesh> CreatePlane(float size = 1.0f);
		Ref<CpuMesh> CreateSphere(float radius = 0.5f, uint32_t slices = 16, uint32_t stacks = 16);

	}

}