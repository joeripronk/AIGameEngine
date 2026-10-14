#pragma once

#include "Forge/Renderer/Shader.h"
#include <nvrhi/vulkan.h>
#include <nvrhi/nvrhi.h>

namespace Forge {

	class VulkanShader : public Shader
	{
	public:
		VulkanShader(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc);
		VulkanShader(const std::string& name, const std::string& filepath);
		~VulkanShader() override;

		void Bind() const override;
		void Unbind() const override;

		void SetInt(const std::string& name, int value) override;
		void SetIntArray(const std::string& name, int* values, uint32_t count) override;
		void SetFloat(const std::string& name, float value) override;
		void SetFloat2(const std::string& name, const glm::vec2& value) override;
		void SetFloat3(const std::string& name, const glm::vec3& value) override;
		void SetFloat4(const std::string& name, const glm::vec4& value) override;
		void SetMat4(const std::string& name, const glm::mat4& value) override;

		const std::string& GetName() const override { return m_Name; }

		// Get the internal nvrhi shader handles
		nvrhi::ShaderHandle GetVertexShader() const { return m_VertexShader; }
		nvrhi::ShaderHandle GetFragmentShader() const { return m_FragmentShader; }

	private:
		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_FragmentShader;
		nvrhi::ShaderHandle m_Shader;
		std::string m_Name;
	};

}
