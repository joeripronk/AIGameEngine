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

		static Ref<Mesh> Create(const std::string& filepath);
	};

}
