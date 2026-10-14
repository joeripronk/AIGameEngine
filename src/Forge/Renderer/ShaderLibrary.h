#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Renderer/Shader.h"

#include <string>
#include <unordered_map>

namespace Forge {

	class ShaderLibrary
	{
	public:
		void Add(const std::string& name, const Ref<Shader>& shader);
		void Add(const Ref<Shader>& shader);
		Ref<Shader> Load(const std::string& filepath);
		Ref<Shader> Load(const std::string& name, const std::string& filepath);

		Ref<Shader> Get(const std::string& name);

		bool Exists(const std::string& name) const;
		uint32_t GetShaderCount() const { return (uint32_t)m_Shaders.size(); }

	private:
		std::unordered_map<std::string, Ref<Shader>> m_Shaders;
	};

}
