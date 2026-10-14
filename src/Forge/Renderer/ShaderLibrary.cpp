#include "fgpch.h"
#include "Forge/Renderer/ShaderLibrary.h"

#include <fstream>
#include <sstream>

namespace Forge {

	void ShaderLibrary::Add(const std::string& name, const Ref<Shader>& shader)
	{
		FG_CORE_ASSERT(!Exists(name), "Shader already exists!");
		m_Shaders[name] = shader;
	}

	void ShaderLibrary::Add(const Ref<Shader>& shader)
	{
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
		FG_CORE_ASSERT(Exists(name), "Shader not found!");
		return m_Shaders.at(name);
	}

	bool ShaderLibrary::Exists(const std::string& name) const
	{
		return m_Shaders.find(name) != m_Shaders.end();
	}

}
