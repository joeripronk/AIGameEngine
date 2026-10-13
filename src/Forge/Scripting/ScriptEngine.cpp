#include "fgpch.h"
#include "Forge/Scripting/ScriptEngine.h"

#include <lua.hpp>

namespace Forge {

	void* ScriptEngine::s_LuaState = nullptr;

	void ScriptEngine::Init()
	{
		// Initialize Lua state
		// s_LuaState = lua_open();
		// lua_baseopen(s_LuaState);
		FG_CORE_INFO("ScriptEngine initialized");
	}

	void ScriptEngine::Shutdown()
	{
		// lua_close(s_LuaState);
		FG_CORE_INFO("ScriptEngine shut down");
	}

	void ScriptEngine::OnRuntimeStart(Scene* scene)
	{
		// TODO: Initialize Lua environment with scene reference
	}

	void ScriptEngine::OnRuntimeStop()
	{
		// TODO: Clean up Lua state
	}

	void ScriptEngine::OnCreateEntity(Entity entity)
	{
		// TODO: Call Lua OnCreate for entity
	}

	void ScriptEngine::OnUpdateEntity(Entity entity, Timestep ts)
	{
		// TODO: Call Lua OnUpdate for entity
	}

	Ref<class ScriptClass> ScriptEngine::GetEntityClass(const std::string& className)
	{
		// TODO: Get script class from Lua
		return nullptr;
	}

}
