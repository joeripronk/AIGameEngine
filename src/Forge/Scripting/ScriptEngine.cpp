#include "fgpch.h"
#include "Forge/Scripting/ScriptEngine.h"

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Components.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <fstream>
#include <sstream>

namespace Forge {

	lua_State* ScriptEngine::s_LuaState = nullptr;

	// Lua bindings for Entity
	static int LuaEntity_GetName(lua_State* L)
	{
		Entity* entity = (Entity*)lua_touserdata(L, 1);
		if (!entity || !(*entity))
		{
			lua_pushnil(L);
			return 1;
		}
		lua_pushstring(L, entity->GetName().c_str());
		return 1;
	}

	static int LuaEntity_GetPosition(lua_State* L)
	{
		Entity* entity = (Entity*)lua_touserdata(L, 1);
		if (!entity || !(*entity))
			return 0;

		auto& tc = entity->GetComponent<TransformComponent>();
		lua_pushnumber(L, tc.Translation.x);
		lua_pushnumber(L, tc.Translation.y);
		lua_pushnumber(L, tc.Translation.z);
		return 3;
	}

	static int LuaEntity_SetPosition(lua_State* L)
	{
		Entity* entity = (Entity*)lua_touserdata(L, 1);
		if (!entity || !(*entity))
			return 0;

		auto& tc = entity->GetComponent<TransformComponent>();
		tc.Translation.x = (float)lua_tonumber(L, 2);
		tc.Translation.y = (float)lua_tonumber(L, 3);
		tc.Translation.z = (float)lua_tonumber(L, 4);
		return 0;
	}

	static int LuaEntity_AddComponent(lua_State* L)
	{
		Entity* entity = (Entity*)lua_touserdata(L, 1);
		if (!entity || !(*entity))
			return 0;

		const char* typeName = lua_tostring(L, 2);
		if (std::string(typeName) == "Rigidbody")
		{
			entity->AddComponent<RigidbodyComponent>();
		}
		else if (std::string(typeName) == "BoxCollider")
		{
			entity->AddComponent<BoxColliderComponent>();
		}
		else if (std::string(typeName) == "SphereCollider")
		{
			entity->AddComponent<SphereColliderComponent>();
		}
		else if (std::string(typeName) == "Mesh")
		{
			// Mesh component requires a model path
			const char* modelPath = lua_tostring(L, 3);
			auto& mc = entity->AddComponent<MeshComponent>();
			mc.ModelPath = modelPath ? modelPath : "";
		}
		else if (std::string(typeName) == "PBRMaterial")
		{
			entity->AddComponent<PBRMaterialComponent>();
		}
		else if (std::string(typeName) == "Light")
		{
			entity->AddComponent<LightComponent>();
		}
		else
		{
			FG_CORE_ERROR("Unknown component type: {0}", typeName);
		}
		return 0;
	}

	static int LuaEntity_RemoveComponent(lua_State* L)
	{
		Entity* entity = (Entity*)lua_touserdata(L, 1);
		if (!entity || !(*entity))
			return 0;

		const char* typeName = lua_tostring(L, 2);
		if (std::string(typeName) == "Rigidbody")
		{
			entity->RemoveComponent<RigidbodyComponent>();
		}
		else if (std::string(typeName) == "BoxCollider")
		{
			entity->RemoveComponent<BoxColliderComponent>();
		}
		else if (std::string(typeName) == "SphereCollider")
		{
			entity->RemoveComponent<SphereColliderComponent>();
		}
		else if (std::string(typeName) == "Mesh")
		{
			entity->RemoveComponent<MeshComponent>();
		}
		else if (std::string(typeName) == "PBRMaterial")
		{
			entity->RemoveComponent<PBRMaterialComponent>();
		}
		else if (std::string(typeName) == "Light")
		{
			entity->RemoveComponent<LightComponent>();
		}
		return 0;
	}

	static int LuaEntity_GetComponent(lua_State* L)
	{
		Entity* entity = (Entity*)lua_touserdata(L, 1);
		if (!entity || !(*entity))
			return 0;

		const char* typeName = lua_tostring(L, 2);
		if (std::string(typeName) == "Rigidbody")
		{
			if (entity->HasComponent<RigidbodyComponent>())
				lua_pushboolean(L, 1);
			else
				lua_pushnil(L);
		}
		else if (std::string(typeName) == "BoxCollider")
		{
			if (entity->HasComponent<BoxColliderComponent>())
				lua_pushboolean(L, 1);
			else
				lua_pushnil(L);
		}
		else
		{
			lua_pushnil(L);
		}
		return 1;
	}

	static int LuaEntity_Destroy(lua_State* L)
	{
		Entity* entity = (Entity*)lua_touserdata(L, 1);
		if (!entity || !(*entity))
			return 0;

		// Mark for destruction (Scene::DestroyEntity is called externally)
		// For now, we just log
		FG_CORE_INFO("Destroying entity: {0}", entity->GetName());
		return 0;
	}

	static int LuaEntity_Duplicate(lua_State* L)
	{
		Entity* entity = (Entity*)lua_touserdata(L, 1);
		if (!entity || !(*entity))
			return 0;

		// Get the scene from the entity
		// We need a way to access the scene from the entity
		// For now, return nil
		lua_pushnil(L);
		return 1;
	}

	// Lua bindings for Scene
	static int LuaScene_CreateEntity(lua_State* L)
	{
		Scene* scene = (Scene*)lua_touserdata(L, 1);
		if (!scene)
			return 0;

		const char* name = lua_tostring(L, 2);
		if (!name)
			name = "Entity";

		Entity entity = scene->CreateEntity(name);
		lua_pushlightuserdata(L, &entity);
		return 1;
	}

	static int LuaScene_DestroyEntity(lua_State* L)
	{
		Scene* scene = (Scene*)lua_touserdata(L, 1);
		if (!scene)
			return 0;

		Entity* entity = (Entity*)lua_touserdata(L, 2);
		if (entity && *entity)
			scene->DestroyEntity(*entity);

		return 0;
	}

	static int LuaScene_GetEntityByName(lua_State* L)
	{
		Scene* scene = (Scene*)lua_touserdata(L, 1);
		if (!scene)
			return 0;

		const char* name = lua_tostring(L, 2);
		Entity entity = scene->FindEntityByName(name);
		lua_pushlightuserdata(L, &entity);
		return 1;
	}

	static int LuaScene_GetEntitiesWithComponent(lua_State* L)
	{
		Scene* scene = (Scene*)lua_touserdata(L, 1);
		if (!scene)
			return 0;

		const char* typeName = lua_tostring(L, 2);
		// Return a table of entities
		lua_newtable(L);

		if (std::string(typeName) == "Rigidbody")
		{
			auto view = scene->GetAllEntitiesWith<RigidbodyComponent>();
			int i = 0;
			for (auto e : view)
			{
				Entity entity = { e, scene };
				lua_pushnumber(L, i + 1);
				lua_pushlightuserdata(L, &entity);
				lua_settable(L, -3);
				i++;
			}
		}
		else if (std::string(typeName) == "Transform")
		{
			auto view = scene->GetAllEntitiesWith<TransformComponent>();
			int i = 0;
			for (auto e : view)
			{
				Entity entity = { e, scene };
				lua_pushnumber(L, i + 1);
				lua_pushlightuserdata(L, &entity);
				lua_settable(L, -3);
				i++;
			}
		}

		return 1;
	}

	// Lua bindings for global functions
	static int LuaSpawnPrefab(lua_State* L)
	{
		// TODO: Implement prefab spawning
		lua_pushnil(L);
		return 1;
	}

	static int LuaGetTime(lua_State* L)
	{
		lua_pushnumber(L, (double)glfwGetTime());
		return 1;
	}

	static int LuaGetDeltaTime(lua_State* L)
	{
		Timestep* ts = (Timestep*)lua_touserdata(L, 1);
		if (ts)
		{
			lua_pushnumber(L, ts->GetSeconds());
		}
		else
		{
			lua_pushnumber(L, 0.016f);
		}
		return 1;
	}

	void ScriptEngine::Init()
	{
		// Use Lua's default allocator (Lua 5.4+ requires non-null allocator)
		s_LuaState = luaL_newstate();
		if (!s_LuaState)
		{
			FG_CORE_ERROR("Failed to create Lua state");
			return;
		}

		// Register global functions
		lua_register(s_LuaState, "GetTime", LuaGetTime);
		lua_register(s_LuaState, "GetDeltaTime", LuaGetDeltaTime);
		lua_register(s_LuaState, "SpawnPrefab", LuaSpawnPrefab);

		// Register Entity metatable
		lua_newtable(s_LuaState);
		lua_pushstring(s_LuaState, "__index");
		lua_newtable(s_LuaState);

		lua_pushstring(s_LuaState, "GetName");
		lua_pushcfunction(s_LuaState, LuaEntity_GetName);
		lua_settable(s_LuaState, -3);

		lua_pushstring(s_LuaState, "GetPosition");
		lua_pushcfunction(s_LuaState, LuaEntity_GetPosition);
		lua_settable(s_LuaState, -3);

		lua_pushstring(s_LuaState, "SetPosition");
		lua_pushcfunction(s_LuaState, LuaEntity_SetPosition);
		lua_settable(s_LuaState, -3);

		lua_pushstring(s_LuaState, "AddComponent");
		lua_pushcfunction(s_LuaState, LuaEntity_AddComponent);
		lua_settable(s_LuaState, -3);

		lua_pushstring(s_LuaState, "RemoveComponent");
		lua_pushcfunction(s_LuaState, LuaEntity_RemoveComponent);
		lua_settable(s_LuaState, -3);

		lua_pushstring(s_LuaState, "GetComponent");
		lua_pushcfunction(s_LuaState, LuaEntity_GetComponent);
		lua_settable(s_LuaState, -3);

		lua_pushstring(s_LuaState, "Destroy");
		lua_pushcfunction(s_LuaState, LuaEntity_Destroy);
		lua_settable(s_LuaState, -3);

		lua_pushstring(s_LuaState, "Duplicate");
		lua_pushcfunction(s_LuaState, LuaEntity_Duplicate);
		lua_settable(s_LuaState, -3);

		lua_settable(s_LuaState, -3); // __index
		lua_setmetatable(s_LuaState, -2);

		// Register Scene metatable
		lua_newtable(s_LuaState);
		lua_pushstring(s_LuaState, "__index");
		lua_newtable(s_LuaState);

		lua_pushstring(s_LuaState, "CreateEntity");
		lua_pushcfunction(s_LuaState, LuaScene_CreateEntity);
		lua_settable(s_LuaState, -3);

		lua_pushstring(s_LuaState, "DestroyEntity");
		lua_pushcfunction(s_LuaState, LuaScene_DestroyEntity);
		lua_settable(s_LuaState, -3);

		lua_pushstring(s_LuaState, "GetEntityByName");
		lua_pushcfunction(s_LuaState, LuaScene_GetEntityByName);
		lua_settable(s_LuaState, -3);

		lua_pushstring(s_LuaState, "GetEntitiesWithComponent");
		lua_pushcfunction(s_LuaState, LuaScene_GetEntitiesWithComponent);
		lua_settable(s_LuaState, -3);

		lua_settable(s_LuaState, -3); // __index
		lua_setmetatable(s_LuaState, -2);

		// Create global "Scene" variable
		lua_pushstring(s_LuaState, "Scene");
		lua_pushlightuserdata(s_LuaState, nullptr); // Placeholder, set in OnRuntimeStart
		lua_settable(s_LuaState, 0);

		FG_CORE_INFO("ScriptEngine initialized");
	}

	void ScriptEngine::Shutdown()
	{
		if (s_LuaState)
		{
			lua_close(s_LuaState);
			s_LuaState = nullptr;
		}
		FG_CORE_INFO("ScriptEngine shut down");
	}

	void ScriptEngine::OnRuntimeStart(Scene* scene)
	{
		if (!s_LuaState)
			Init();

		// Set global Scene reference
		lua_pushstring(s_LuaState, "Scene");
		lua_pushlightuserdata(s_LuaState, scene);
		lua_settable(s_LuaState, 0);

		// Load and execute Lua scripts for ScriptComponent entities
		auto view = scene->GetAllEntitiesWith<ScriptComponent>();
		for (auto e : view)
		{
			Entity entity = { e, scene };
			auto& sc = entity.GetComponent<ScriptComponent>();

			// Load script file
			std::string scriptPath = "resources/scripts/" + sc.ClassName + ".lua";
			std::ifstream file(scriptPath);
			if (!file.is_open())
			{
				FG_CORE_ERROR("Failed to open script: {0}", scriptPath);
				continue;
			}

			std::stringstream ss;
			ss << file.rdbuf();
			std::string content = ss.str();

			if (luaL_dostring(s_LuaState, content.c_str()) != 0)
			{
				FG_CORE_ERROR("Lua error in {0}: {1}", scriptPath, lua_tostring(s_LuaState, -1));
				lua_pop(s_LuaState, 1);
			}

			OnCreateEntity(entity);
		}
	}

	void ScriptEngine::OnRuntimeStop()
	{
		// Clean up Lua state
		FG_CORE_INFO("ScriptEngine runtime stop");
	}

	void ScriptEngine::OnCreateEntity(Entity entity)
	{
		if (!s_LuaState)
			return;

		// Call OnCreate in Lua context
		// Push entity onto the stack
		lua_pushlightuserdata(s_LuaState, &entity);

		// Call entity.OnCreate if it exists
		// This is handled by the Lua script's OnCreate function
		// For now, we just log
		FG_CORE_TRACE("Entity created: {0}", entity.GetName());
	}

	void ScriptEngine::OnUpdateEntity(Entity entity, Timestep ts)
	{
		if (!s_LuaState)
			return;

		// Call OnUpdate in Lua context
		// Push entity and timestep
		lua_pushlightuserdata(s_LuaState, &entity);
		lua_pushlightuserdata(s_LuaState, &ts);

		// Call entity.OnUpdate if it exists
		// This is handled by the Lua script's OnUpdate function
		// For now, we just log
		FG_CORE_TRACE("Entity updated: {0}", entity.GetName());
	}

	Ref<ScriptClass> ScriptEngine::GetEntityClass(const std::string& className)
	{
		(void)className;
		// TODO: Implement Lua-based script class resolution
		return nullptr;
	}

}
