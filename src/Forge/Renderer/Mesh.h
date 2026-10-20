#pragma once

#include "Forge/Renderer/Texture.h"

#include <string>
#include <vector>

namespace Forge {

	struct Vertex
	{
		glm::vec3 Position;
		glm::vec3 Normal;
		glm::vec2 UV;
		glm::vec3 Tangent;
		glm::vec3 Bitangent;
	};

	// The per-instance data: the model matrix, as the vertex attributes
	// read it. Four rows, because a matrix is four vec4s to the hardware.
	struct InstanceData
	{
		glm::vec4 Row0 = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
		glm::vec4 Row1 = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
		glm::vec4 Row2 = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		glm::vec4 Row3 = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

		[[nodiscard]] static InstanceData FromMatrix(const glm::mat4& matrix);
	};

	struct Material
	{
		glm::vec3 Albedo = { 1.0f, 1.0f, 1.0f };
		float Metallic = 0.0f;
		float Roughness = 0.5f;
		float AO = 1.0f;
		glm::vec3 Emissive = { 0.0f, 0.0f, 0.0f };
		float EmissiveIntensity = 0.0f;

		Ref<Texture> AlbedoTexture;
		Ref<Texture> MetallicRoughnessTexture;
		Ref<Texture> AOTexture;
		Ref<Texture> EmissiveTexture;
		Ref<Texture> NormalTexture;
	};

	class Mesh
	{
	public:
		virtual ~Mesh() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		virtual uint32_t GetIndexCount() const = 0;
		virtual const Ref<class VertexArray>& GetVertexArray() const = 0;

		static Ref<Mesh> Create(const std::string& filepath);

		// Uploads a CPU mesh to the GPU. Used for the built-in primitives.
		static Ref<Mesh> CreateFromCpu(class CpuMesh* cpuMesh, const Material& material);
	};

}
