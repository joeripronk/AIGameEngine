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

		// Dispatches a physics contact into the entity's Lua script table,
		// calling OnCollision (contact began) or OnCollisionEnd (contact ended)
		// if the script defines it. Both are optional.
		//
		// Signature: OnCollision(other, nx, ny, nz)
		//            OnCollisionEnd(other, nx, ny, nz)
		// where `other` is nil if the partner body has no entity.
		static void OnEntityCollision(Entity self, Entity other, const glm::vec3& normal, Physics::ContactPhase phase);

		// Attaches this engine as the collision bridge for every scripted entity
		// in `scene`, so Lua scripts receive contacts without per-entity setup.
		// Safe to call before or after playback starts.
		static void BindSceneCollisionHandlers(Scene* scene);

		// The scene currently exposed to scripts (nullptr between runtimes).
		static Scene* GetActiveScene() { return s_ActiveScene; }

		// Directory scripts are resolved from. Defaults to "resources/scripts/".
		static void SetScriptDirectory(const std::string& directory) { s_ScriptDirectory = directory; }
		static const std::string& GetScriptDirectory() { return s_ScriptDirectory; }

		// --- Tooling / embedding API (also used by tests and the editor) ----

		// Executes a raw Lua string in the global environment.
		[[nodiscard]] static bool ExecuteString(const std::string& code, std::string* errorOut = nullptr);

		// Loads a script file, associates it with `entity`, and calls OnCreate.
		[[nodiscard]] static bool LoadScriptForEntity(Entity entity, const std::string& filepath, std::string* errorOut = nullptr);

		// Calls a named function on an entity's loaded script table.
		static bool CallEntityFunction(Entity entity, const char* functionName, float dt = 0.0f);

		// Directly invokes OnUpdate for the given entity (used by Scene).
		static bool HasScript(Entity entity);

		// Whether the engine currently owns a Lua state.
		static bool IsInitialized() { return s_LuaState != nullptr; }

		// Returns the tostring() of a global variable (useful for tests/AI).
		static std::string GetGlobalAsString(const std::string& name);

		// Reads a field out of an entity's script table. This is how a script's
		// own state (hit counters, last contact, and so on) becomes observable to
		// tests, tools and AI agents without the script needing a special export.
		//
		// Returns false if the entity has no script, the field is absent, or the
		// field is not a number/string.
		[[nodiscard]] static bool GetScriptNumber(Entity entity, const char* field, double& out);
		[[nodiscard]] static bool GetScriptString(Entity entity, const char* field, std::string& out);

		// Writes a number into an entity's script table, so tools can drive a
		// script's inputs the same way Lua would.
		[[nodiscard]] static bool SetScriptNumber(Entity entity, const char* field, double value);

	private:
		// Pushes the entity's script table onto the stack. Returns false (leaving
		// the stack untouched) when there is no script for that entity.
		static bool PushEntityScriptTable(Entity entity);

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
