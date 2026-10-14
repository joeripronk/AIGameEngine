#include "fgpch.h"
#include "Platform/Vulkan/VulkanShader.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/common.h>
#include <nvrhi/shader_library.h>

#include <fstream>
#include <sstream>

namespace Forge {

	VulkanShader::VulkanShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
		: m_Name(name)
	{
		CreateShader(name, vertexSrc, fragmentSrc);
	}

	VulkanShader::VulkanShader(const std::string& name, const std::string& filepath)
		: m_Name(name)
	{
		std::ifstream stream(filepath);
		if (!stream.is_open())
		{
			FG_CORE_ERROR("Failed to open shader file: {0}", filepath);
			return;
		}

		std::stringstream ss;
		ss << stream.rdbuf();
		std::string content = ss.str();

		CreateShader(name, content);
	}

	VulkanShader::~VulkanShader()
	{
	}

	void VulkanShader::CreateShader(const std::string& name, const std::string& content)
	{
		nvrhi::ShaderDesc desc;
		desc.sourceLanguage = nvrhi::ShaderLanguage::HLSL;
		desc.entryPoint = "main";
		desc.debugName = name;
		desc.code = content.c_str();

		m_Shader = s_Device->createShader(desc);
		m_Reflection = s_Device->getShaderReflection(m_Shader);

		// Build binding map
		for (uint32_t i = 0; i < m_Reflection->getBindingCount(); i++)
		{
			nvrhi::BindingInfo binding = m_Reflection->getBindingByIndex(i);
			m_BindingMap[binding.name] = binding;
		}

		FG_CORE_INFO("Shader loaded: {0}", name);
	}

	void VulkanShader::CreateShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		// Combine vertex and fragment shaders into a single HLSL shader
		std::string combined = vertexSrc + "\n" + fragmentSrc;
		CreateShader(name, combined);
	}

	void VulkanShader::Bind() const
	{
		// Bind shader to command list
		nvrhi::ShaderBindingHandle binding = nvrhi::vulkan::GetShaderBinding(s_Device, m_Shader);
		nvrhi::vulkan::SetShaderBinding(s_Device, binding);
	}

	void VulkanShader::Unbind() const
	{
		// No explicit unbind needed with nvrhi
	}

	void VulkanShader::SetInt(const std::string& name, int value)
	{
		auto it = m_BindingMap.find(name);
		if (it != m_BindingMap.end())
		{
			nvrhi::vulkan::SetConstant(s_Device, it->second, value);
		}
	}

	void VulkanShader::SetIntArray(const std::string& name, int* values, uint32_t count)
	{
		auto it = m_BindingMap.find(name);
		if (it != m_BindingMap.end())
		{
			nvrhi::vulkan::SetConstant(s_Device, it->second, values, count);
		}
	}

	void VulkanShader::SetFloat(const std::string& name, float value)
	{
		auto it = m_BindingMap.find(name);
		if (it != m_BindingMap.end())
		{
			nvrhi::vulkan::SetConstant(s_Device, it->second, value);
		}
	}

	void VulkanShader::SetFloat2(const std::string& name, const glm::vec2& value)
	{
		auto it = m_BindingMap.find(name);
		if (it != m_BindingMap.end())
		{
			nvrhi::vulkan::SetConstant(s_Device, it->second, glm::vec4(value, 0.0f, 1.0f));
		}
	}

	void VulkanShader::SetFloat3(const std::string& name, const glm::vec3& value)
	{
		auto it = m_BindingMap.find(name);
		if (it != m_BindingMap.end())
		{
			nvrhi::vulkan::SetConstant(s_Device, it->second, glm::vec4(value, 1.0f));
		}
	}

	void VulkanShader::SetFloat4(const std::string& name, const glm::vec4& value)
	{
		auto it = m_BindingMap.find(name);
		if (it != m_BindingMap.end())
		{
			nvrhi::vulkan::SetConstant(s_Device, it->second, value);
		}
	}

	void VulkanShader::SetMat4(const std::string& name, const glm::mat4& value)
	{
		auto it = m_BindingMap.find(name);
		if (it != m_BindingMap.end())
		{
			nvrhi::vulkan::SetConstant(s_Device, it->second, value);
		}
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
