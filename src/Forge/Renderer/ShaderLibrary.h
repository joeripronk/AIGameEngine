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

		// Drops every shader. A compiled shader holds GPU objects, and the
		// renderer's library is a static - so left to static destruction, those
		// GPU objects are destroyed after the device they came from. The
		// renderer clears the library while shutting down, with the device
		// still alive.
		void Clear() { m_Shaders.clear(); }

	private:
		std::unordered_map<std::string, Ref<Shader>> m_Shaders;
	};

}
