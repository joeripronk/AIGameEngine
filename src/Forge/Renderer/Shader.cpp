#include "fgpch.h"
#include "Forge/Core/Base.h"
#include "Forge/Renderer/Shader.h"
#include "Forge/Renderer/ShaderLibrary.h"

#if !defined(FG_NO_VULKAN_RUNTIME)
	#include "Platform/Vulkan/VulkanShader.h"
#endif

#include <fstream>
#include <sstream>

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	Ref<Shader> Shader::Create(const std::string& filepath)
	{
		// Read shader file
		std::ifstream stream(filepath);
		if (!stream.is_open())
		{
			FG_CORE_ERROR("Failed to open shader file: {0}", filepath);
			return nullptr;
		}

		std::stringstream ss;
		ss << stream.rdbuf();
		std::string content = ss.str();

		return Create("Shader", content);
	}

	Ref<Shader> Shader::Create(const char* name, const std::string& content)
	{
		// Parse shader content (simplified - expects "vertex" and "fragment" sections)
		std::string vertexSrc, fragmentSrc;
		bool readingVertex = false;
		bool readingFragment = false;

		std::istringstream stream(content);
		std::string line;
		while (std::getline(stream, line))
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

		// If no sections found, use the whole content as vertex shader
		if (vertexSrc.empty() && fragmentSrc.empty())
		{
			vertexSrc = content;
			fragmentSrc = "";
		}

		return Create(std::string(name), vertexSrc, fragmentSrc);
	}

	Ref<Shader> Shader::Create(const std::string& name, const std::string& vertexSrc, const std::string& fragmentSrc)
	{
		// GLSL source requires a runtime compiler, which the engine does not
		// ship. Shaders are pre-compiled to SPIR-V (see resources/shaders).
		(void)name;
		(void)vertexSrc;
		(void)fragmentSrc;
		FG_CORE_ERROR("Shader::Create(GLSL) is unsupported; use pre-compiled SPIR-V (CreateFromSPIRV)");
		return nullptr;
	}

	Ref<Shader> Shader::CreateFromSPIRV(const std::string& name, const std::string& vertexSpvPath, const std::string& fragmentSpvPath)
	{
		return CreateRef<VulkanShader>(name, vertexSpvPath, fragmentSpvPath);
	}

	void ShaderLibrary::Add(const std::string& name, const Ref<Shader>& shader)
	{
		if (!shader)
		{
			FG_CORE_WARN("ShaderLibrary: refusing to add null shader '{0}'", name);
			return;
		}

		FG_CORE_ASSERT_MSG(!Exists(name), "Shader already exists!");
		m_Shaders[name] = shader;
	}

	void ShaderLibrary::Add(const Ref<Shader>& shader)
	{
		if (!shader)
			return;
		Add(shader->GetName(), shader);
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& filepath)
	{
		std::string name;
		// Extract name from filepath
		size_t lastSlash = filepath.find_last_of("/\\");
		if (lastSlash != std::string::npos)
			name = filepath.substr(lastSlash + 1);
		else
			name = filepath;

		// Remove extension
		size_t lastDot = name.find_last_of('.');
		if (lastDot != std::string::npos)
			name = name.substr(0, lastDot);

		return Load(name, filepath);
	}

	Ref<Shader> ShaderLibrary::Load(const std::string& name, const std::string& filepath)
	{
		Ref<Shader> shader = Shader::Create(filepath);
		Add(name, shader);
		return shader;
	}

	Ref<Shader> ShaderLibrary::Get(const std::string& name)
	{
		FG_CORE_ASSERT_MSG(Exists(name), "Shader not found!");
		return m_Shaders.at(name);
	}

	bool ShaderLibrary::Exists(const std::string& name) const
	{
		return m_Shaders.find(name) != m_Shaders.end();
	}

}

#endif // FG_NO_VULKAN_RUNTIME
