#include "fgpch.h"
#include "Platform/Vulkan/VulkanShader.h"
#include "Platform/Vulkan/VulkanRendererAPI.h"

#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

#include "Forge/Core/FileSystem.h"

namespace Forge {

	namespace {

		bool LoadSPIRV(const std::string& path, std::vector<uint8_t>& out)
		{
			if (!FileSystem::ReadFile(path, out))
			{
				FG_CORE_ERROR("VulkanShader: cannot read SPIR-V '{0}'", path);
				return false;
			}

			if (out.empty())
			{
				FG_CORE_ERROR("VulkanShader: SPIR-V '{0}' is empty", path);
				return false;
			}

			return true;
		}

	}

	VulkanShader::VulkanShader(const std::string& name, const std::string& vertexSpvPath, const std::string& fragmentSpvPath)
		: m_Name(name)
	{
		auto device = VulkanRendererAPI::GetStaticDevice();
		if (!device)
		{
			FG_CORE_ERROR("VulkanShader '{0}': renderer device not initialized", name);
			return;
		}

		std::vector<uint8_t> vertexCode, fragmentCode;
		if (!LoadSPIRV(vertexSpvPath, vertexCode) || !LoadSPIRV(fragmentSpvPath, fragmentCode))
		{
			FG_CORE_ERROR("VulkanShader '{0}': failed to load SPIR-V modules", name);
			return;
		}

		nvrhi::ShaderDesc desc;
		desc.entryName = "main";
		desc.shaderType = nvrhi::ShaderType::Vertex;
		desc.debugName = name + ".vert";
		m_VertexShader = device->createShader(desc, vertexCode.data(), vertexCode.size());

		desc.shaderType = nvrhi::ShaderType::Pixel;
		desc.debugName = name + ".frag";
		m_FragmentShader = device->createShader(desc, fragmentCode.data(), fragmentCode.size());

		if (!m_VertexShader || !m_FragmentShader)
		{
			FG_CORE_ERROR("VulkanShader '{0}': failed to create nvrhi shader modules", name);
			return;
		}

		FG_CORE_INFO("VulkanShader '{0}' created from SPIR-V", name);
	}

	VulkanShader::~VulkanShader()
	{
	}

	void VulkanShader::Bind() const
	{
		// The renderer binds the concrete graphics pipeline; uniform data is
		// pushed by the renderer into its per-frame/object constant buffers.
	}

	void VulkanShader::Unbind() const
	{
	}

	void VulkanShader::SetFloatArray(const std::string& name, const std::vector<float>& values)
	{
		for (auto& entry : m_Floats)
		{
			if (entry.first == name)
			{
				entry.second = values;
				return;
			}
		}
		m_Floats.emplace_back(name, values);
	}

	void VulkanShader::SetInt(const std::string& name, int value)
	{
		for (auto& entry : m_Ints)
		{
			if (entry.first == name)
			{
				entry.second = { value };
				return;
			}
		}
		m_Ints.emplace_back(name, std::vector<int>{ value });
	}

	void VulkanShader::SetIntArray(const std::string& name, int* values, uint32_t count)
	{
		for (auto& entry : m_Ints)
		{
			if (entry.first == name)
			{
				entry.second.assign(values, values + count);
				return;
			}
		}
		m_Ints.emplace_back(name, std::vector<int>(values, values + count));
	}

	void VulkanShader::SetFloat(const std::string& name, float value)
	{
		SetFloatArray(name, { value });
	}

	void VulkanShader::SetFloat2(const std::string& name, const glm::vec2& value)
	{
		SetFloatArray(name, { value.x, value.y });
	}

	void VulkanShader::SetFloat3(const std::string& name, const glm::vec3& value)
	{
		SetFloatArray(name, { value.x, value.y, value.z });
	}

	void VulkanShader::SetFloat4(const std::string& name, const glm::vec4& value)
	{
		SetFloatArray(name, { value.x, value.y, value.z, value.w });
	}

	void VulkanShader::SetMat4(const std::string& name, const glm::mat4& value)
	{
		std::vector<float> flat(16);
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j)
				flat[(size_t)i * 4 + j] = value[i][j];
		SetFloatArray(name, flat);
	}

}