#include "fgpch.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <fstream>
#include <sstream>
#include <cstring>

namespace Forge {

	VulkanShader::VulkanShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
		: m_Name(name)
	{
		// For production, shaders would be pre-compiled to SPIR-V at build time.
		// For now, we create a simple shader from the source.
		// The actual shader compilation requires glslang to be available.
		// This is a stub that creates a basic shader for testing.

		// Create a simple vertex shader (passthrough)
		const char* simpleVertexSrc = R"(
			#version 450
			layout(location = 0) in vec3 aPosition;
			layout(location = 1) in vec3 aNormal;
			layout(location = 2) in vec2 aUV;
			layout(location = 3) in vec3 aTangent;
			layout(location = 4) in vec3 aBitangent;
			layout(location = 0) out vec3 vPosition;
			layout(location = 1) out vec3 vNormal;
			layout(location = 2) out vec2 vUV;
			layout(location = 3) out vec3 vTangent;
			layout(location = 4) out vec3 vBitangent;
			layout(location = 5) out vec3 vWorldPosition;
			uniform mat4 u_ViewProjection;
			uniform mat4 u_Transform;
			void main() {
				vPosition = aPosition;
				vNormal = aNormal;
				vUV = aUV;
				vTangent = aTangent;
				vBitangent = aBitangent;
				vWorldPosition = (u_Transform * vec4(aPosition, 1.0)).xyz;
				gl_Position = u_ViewProjection * u_Transform * vec4(aPosition, 1.0);
			}
		)";

		// Create a simple fragment shader (solid color)
		const char* simpleFragmentSrc = R"(
			#version 450
			layout(location = 0) in vec3 vPosition;
			layout(location = 1) in vec3 vNormal;
			layout(location = 2) in vec2 vUV;
			layout(location = 3) in vec3 vTangent;
			layout(location = 4) in vec3 vBitangent;
			layout(location = 5) in vec3 vWorldPosition;
			layout(location = 0) out vec4 oColor;
			uniform vec3 u_Albedo;
			uniform float u_Metallic;
			uniform float u_Roughness;
			uniform float u_AO;
			uniform vec3 u_Emissive;
			uniform float u_EmissiveIntensity;
			void main() {
				oColor = vec4(u_Albedo, 1.0);
			}
		)";

		// Use the provided source or fall back to simple shaders
		const char* vsSrc = !vertexSrc.empty() ? vertexSrc.c_str() : simpleVertexSrc;
		const char* fsSrc = !fragmentSrc.empty() ? fragmentSrc.c_str() : simpleFragmentSrc;

		// For now, create a simple shader stub
		// In production, this would use glslang to compile GLSL to SPIR-V
		// and then create nvrhi shaders from the SPIR-V bytecode
		FG_CORE_INFO("Shader created (stub): {0}", name);
	}

	VulkanShader::VulkanShader(const std::string& name, const std::string& filepath)
		: m_Name(name)
	{
		// Load shader from file
		std::ifstream stream(filepath);
		if (!stream.is_open())
		{
			FG_CORE_ERROR("Failed to open shader file: {0}", filepath);
			return;
		}

		std::stringstream ss;
		ss << stream.rdbuf();
		std::string content = ss.str();

		// Parse shader content
		std::string vertexSrc, fragmentSrc;
		bool readingVertex = false;
		bool readingFragment = false;

		std::istringstream stream2(content);
		std::string line;
		while (std::getline(stream2, line))
		{
			if (line.find("vertex") != std::string::npos)
			{
				readingVertex = true;
				readingFragment = false;
				continue;
			}
			if (line.find("fragment") != std::string::npos)
			{
				readingVertex = false;
				readingFragment = true;
				continue;
			}
			if (readingVertex)
				vertexSrc += line + "\n";
			if (readingFragment)
				fragmentSrc += line + "\n";
		}

		if (vertexSrc.empty() && fragmentSrc.empty())
		{
			vertexSrc = content;
			fragmentSrc = "";
		}

		FG_CORE_INFO("Shader created from file (stub): {0}", name);
	}

	VulkanShader::~VulkanShader()
	{
	}

	void VulkanShader::Bind() const
	{
		// In production, this would bind the shader pipeline
		// For now, it's a stub
	}

	void VulkanShader::Unbind() const
	{
		// In production, this would unbind the shader pipeline
	}

	void VulkanShader::SetInt(const std::string& name, int value)
	{
		(void)name;
		(void)value;
		// TODO: Set uniform value
	}

	void VulkanShader::SetIntArray(const std::string& name, int* values, uint32_t count)
	{
		(void)name;
		(void)values;
		(void)count;
		// TODO: Set uniform array value
	}

	void VulkanShader::SetFloat(const std::string& name, float value)
	{
		(void)name;
		(void)value;
		// TODO: Set uniform value
	}

	void VulkanShader::SetFloat2(const std::string& name, const glm::vec2& value)
	{
		(void)name;
		(void)value;
		// TODO: Set uniform value
	}

	void VulkanShader::SetFloat3(const std::string& name, const glm::vec3& value)
	{
		(void)name;
		(void)value;
		// TODO: Set uniform value
	}

	void VulkanShader::SetFloat4(const std::string& name, const glm::vec4& value)
	{
		(void)name;
		(void)value;
		// TODO: Set uniform value
	}

	void VulkanShader::SetMat4(const std::string& name, const glm::mat4& value)
	{
		(void)name;
		(void)value;
		// TODO: Set uniform value
	}

	Ref<Shader> Shader::Create(const std::string& filepath)
	{
		// Extract name from filepath
		std::string name;
		size_t lastSlash = filepath.find_last_of("/\\");
		if (lastSlash != std::string::npos)
			name = filepath.substr(lastSlash + 1);
		else
			name = filepath;

		size_t lastDot = name.find_last_of('.');
		if (lastDot != std::string::npos)
			name = name.substr(0, lastDot);

		return CreateRef<VulkanShader>(name, filepath);
	}

	Ref<Shader> Shader::Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		return CreateRef<VulkanShader>(name, vertexSrc, fragmentSrc);
	}

}
