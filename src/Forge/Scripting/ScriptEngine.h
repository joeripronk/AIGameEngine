#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Core/Timestep.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"

#include <cstdint>
#include <string>
#include <unordered_map>

extern "C" {
#include <lua.h>
}

namespace Forge {

	// The Lua scripting engine. Scripts are ordinary Lua chunks that return a
	// table containing optional `OnCreate(entity)`, `OnUpdate(entity, dt)` and
	// `OnDestroy(entity)` functions. Entities and the scene are exposed to Lua
	// as full userdata guarded by metatables.
	class ScriptEngine
	{
	public:
		static void Init();
		static void Shutdown();

		static void OnRuntimeStart(Scene* scene);
		static void OnRuntimeStop();

		static void OnCreateEntity(Entity entity);
		static void OnUpdateEntity(Entity entity, Timestep ts);
		static void OnDestroyEntity(Entity entity);

		// The scene currently exposed to scripts (nullptr between runtimes).
		static Scene* GetActiveScene() { return s_ActiveScene; }

		// Directory scripts are resolved from. Defaults to "resources/scripts/".
		static void SetScriptDirectory(const std::string& directory) { s_ScriptDirectory = directory; }
		static const std::string& GetScriptDirectory() { return s_ScriptDirectory; }

		// --- Tooling / embedding API (also used by tests and the editor) ----

		// Executes a raw Lua string in the global environment.
		static bool ExecuteString(const std::string& code, std::string* errorOut = nullptr);

		// Loads a script file, associates it with `entity`, and calls OnCreate.
		static bool LoadScriptForEntity(Entity entity, const std::string& filepath, std::string* errorOut = nullptr);

		// Calls a named function on an entity's loaded script table.
		static bool CallEntityFunction(Entity entity, const char* functionName, float dt = 0.0f);

		// Directly invokes OnUpdate for the given entity (used by Scene).
		static bool HasScript(Entity entity);

		// Whether the engine currently owns a Lua state.
		static bool IsInitialized() { return s_LuaState != nullptr; }

		// Returns the tostring() of a global variable (useful for tests/AI).
		static std::string GetGlobalAsString(const std::string& name);

	private:
		static void RegisterBindings();
		static void ReleaseEntityScript(uint32_t entityID);

		static lua_State* s_LuaState;
		static Scene* s_ActiveScene;
		static std::string s_ScriptDirectory;

		// entity id -> luaL_ref into the registry for that entity's script table
		static std::unordered_map<uint32_t, int> s_EntityScripts;
		// class name -> luaL_ref for the compiled source chunk (reused per spawn)
		static std::unordered_map<std::string, std::string> s_ScriptSourceCache;
	};

}
