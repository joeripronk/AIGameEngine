#include "fgpch.h"
#include "Platform/Vulkan/VulkanShader.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include <fstream>
#include <sstream>

namespace Forge {

	VulkanShader::VulkanShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
		: m_Name(name)
	{
		(void)vertexSrc; (void)fragmentSrc;
		// Stub - shader compilation requires HLSL bytecode
		// TODO: Compile shaders using shaderc or spirv-cross
		FG_CORE_INFO("Shader created (stub): {0}", name);
	}

	VulkanShader::VulkanShader(const std::string& name, const std::string& filepath)
		: m_Name(name)
	{
		(void)filepath;
		// Stub - shader compilation requires HLSL bytecode
		FG_CORE_INFO("Shader created from file (stub): {0}", name);
	}

	VulkanShader::~VulkanShader()
	{
	}

	void VulkanShader::Bind() const
	{
		// Stub - pipeline is not implemented
	}

	void VulkanShader::Unbind() const
	{
		// Stub - pipeline is not implemented
	}

	void VulkanShader::SetInt(const std::string& name, int value)
	{
		(void)name; (void)value;
	}

	void VulkanShader::SetIntArray(const std::string& name, int* values, uint32_t count)
	{
		(void)name; (void)values; (void)count;
	}

	void VulkanShader::SetFloat(const std::string& name, float value)
	{
		(void)name; (void)value;
	}

	void VulkanShader::SetFloat2(const std::string& name, const glm::vec2& value)
	{
		(void)name; (void)value;
	}

	void VulkanShader::SetFloat3(const std::string& name, const glm::vec3& value)
	{
		(void)name; (void)value;
	}

	void VulkanShader::SetFloat4(const std::string& name, const glm::vec4& value)
	{
		(void)name; (void)value;
	}

	void VulkanShader::SetMat4(const std::string& name, const glm::mat4& value)
	{
		(void)name; (void)value;
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

#endif // FG_NO_VULKAN_RUNTIME
