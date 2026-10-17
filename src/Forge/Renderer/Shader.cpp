#include "fgpch.h"
#include "Forge/Core/Base.h"
#include "Forge/Renderer/Shader.h"
#include "Forge/Renderer/ShaderLibrary.h"

#if !defined(FG_NO_VULKAN_RUNTIME)
	#include "Platform/Vulkan/VulkanShader.h"
#endif

#include "Forge/Core/FileSystem.h"

#if !defined(FG_NO_VULKAN_RUNTIME)

namespace Forge {

	Ref<Shader> Shader::Create(const std::string& filepath)
	{
		std::string content;
		if (!FileSystem::ReadTextFile(filepath, content))
		{
			FG_CORE_ERROR("Failed to open shader file: {0}", filepath);
			return nullptr;
		}

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

}

#endif // FG_NO_VULKAN_RUNTIME
