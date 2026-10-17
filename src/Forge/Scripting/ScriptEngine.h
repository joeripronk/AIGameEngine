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

		// Predicate: a runtime session is live, so hot reloads have entities
		// to rebind. Safe to poll, safe to ignore.
		static bool IsRuntimeActive() { return s_ActiveScene != nullptr; }

		// Directory scripts are resolved from. Defaults to "resources/scripts/".
		static void SetScriptDirectory(const std::string& directory) { s_ScriptDirectory = directory; }
		static const std::string& GetScriptDirectory() { return s_ScriptDirectory; }

		// The path a class name resolves to, as OnRuntimeStart loads it.
		static std::string ResolveScriptPath(const std::string& className);

		// --- Hot reload (the Hazel script-reload workflow) ------------------

		// Re-reads the file and rebuilds every live script instance bound to
		// it, calling OnCreate on each fresh instance. Per-instance state
		// starts clean by design: the chunk runs again, which is the whole
		// point of editing a script while the game runs.
		//
		// Reload is verified, not assumed: a file that cannot be read, a
		// chunk that fails to compile or run, or a chunk that stops returning
		// a table keeps that entity's previous instance running - the same
		// rule as the texture cache, where a failed reload keeps the texture
		// that worked. Returns false when any bound instance failed to reload,
		// naming each one in the log.
		//
		// True with nothing bound is honest: the file is no longer cached, and
		// there was nothing to rebuild. Outside a runtime session there are no
		// instances, so this reports failure rather than pretending.
		[[nodiscard]]
		static bool HotReloadScript(const std::string& filepath);

		// Hot reloads every script file the live session currently uses.
		// Returns how many files reloaded cleanly; the caller counts that
		// against the files it asked about.
		static size_t HotReloadAll();

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

		// Booleans and strings are handled explicitly rather than through
		// lua_toboolean, which would make a missing field indistinguishable
		// from a real false - the failure mode this API exists to rule out.
		[[nodiscard]] static bool GetScriptBool(Entity entity, const char* field, bool& out);
		[[nodiscard]] static bool SetScriptBool(Entity entity, const char* field, bool value);
		[[nodiscard]] static bool SetScriptString(Entity entity, const char* field, const std::string& value);

		// Delta time, in seconds.
		//
		// AGENTS.md documented GetDeltaTime in the Lua API and no such binding
		// existed, so a script calling it got "attempt to call a nil value".
		//
		// GetEntityDeltaTime returns the dt of the frame `entity` was last
		// updated in, or 0.0 if it never has been. GetLastDeltaTime is the
		// current frame's, which is what a script with no entity in hand wants.
		// Reporting 0.0 is honest rather than a fallback: a script calling this
		// outside OnUpdate has no frame to describe.
		[[nodiscard]] static double GetEntityDeltaTime(Entity entity);
		[[nodiscard]] static double GetLastDeltaTime();

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
		// entity id -> the file its instance was built from, so a hot reload
		// can find the instances bound to a changed file
		static std::unordered_map<uint32_t, std::string> s_EntityScriptPaths;
		// class name -> luaL_ref for the compiled source chunk (reused per spawn)
		static std::unordered_map<std::string, std::string> s_ScriptSourceCache;

		// Delta times of the current frame, in seconds.
		//
		// Per entity rather than a single global, because dt is a property of a
		// call: two entities updated in the same frame share a value, but an
		// entity that is not being updated must not report a stale one as if it
		// were current.
		static std::unordered_map<uint32_t, double> s_EntityDeltaTimes;

		// The newest of the above, which is what a script with no entity
		// argument means by "the delta time".
		static double s_LastDeltaTime;
	};

}
