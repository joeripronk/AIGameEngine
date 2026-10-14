#include "fgpch.h"
#include "Forge/Scripting/ScriptEngine.h"

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Prefab.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <fstream>
#include <sstream>

namespace Forge {

	lua_State* ScriptEngine::s_LuaState = nullptr;
	Scene* ScriptEngine::s_ActiveScene = nullptr;
	std::string ScriptEngine::s_ScriptDirectory = "resources/scripts/";
	std::unordered_map<uint32_t, int> ScriptEngine::s_EntityScripts;
	std::unordered_map<std::string, std::string> ScriptEngine::s_ScriptSourceCache;

	namespace {

		constexpr const char* kEntityMeta = "Forge.Entity";
		constexpr const char* kSceneMeta = "Forge.Scene";

		// --- userdata helpers -------------------------------------------------

		Entity* TestEntity(lua_State* L, int index)
		{
			return static_cast<Entity*>(luaL_testudata(L, index, kEntityMeta));
		}

		Scene* TestScene(lua_State* L, int index)
		{
			void* storage = luaL_testudata(L, index, kSceneMeta);
			if (!storage)
				return nullptr;
			return *static_cast<Scene**>(storage);
		}

		// Resolves the scene argument for Scene methods. Supports both colon
		// calls (`Scene:CreateEntity(x)`) and dot calls (`Scene.CreateEntity(x)`).
		Scene* GetSceneArg(lua_State* L)
		{
			Scene* scene = TestScene(L, 1);
			if (!scene)
				scene = ScriptEngine::GetActiveScene();
			return scene;
		}

		int PushEntity(lua_State* L, const Entity& entity)
		{
			void* storage = lua_newuserdatauv(L, sizeof(Entity), 0);
			new (storage) Entity(entity);
			luaL_setmetatable(L, kEntityMeta);
			return 1;
		}

		int PushNilEntity(lua_State* L)
		{
			lua_pushnil(L);
			return 1;
		}

		const char* SafeString(lua_State* L, int index, const char* fallback = "")
		{
			const char* value = lua_tostring(L, index);
			return value ? value : fallback;
		}

		// --- component property access ---------------------------------------

		bool GetComponentVector3(Entity& entity, const std::string& comp, const std::string& field, glm::vec3& out)
		{
			if (comp == "Transform")
			{
				auto& c = entity.GetComponent<TransformComponent>();
				if (field == "Translation") { out = c.Translation; return true; }
				if (field == "Rotation") { out = c.Rotation; return true; }
				if (field == "Scale") { out = c.Scale; return true; }
			}
			else if (comp == "BoxCollider" && entity.HasComponent<BoxColliderComponent>())
			{
				auto& c = entity.GetComponent<BoxColliderComponent>();
				if (field == "Offset") { out = c.Offset; return true; }
				if (field == "Size") { out = c.Size; return true; }
			}
			else if (comp == "SphereCollider" && entity.HasComponent<SphereColliderComponent>())
			{
				auto& c = entity.GetComponent<SphereColliderComponent>();
				if (field == "Offset") { out = c.Offset; return true; }
			}
			else if (comp == "Light" && entity.HasComponent<LightComponent>())
			{
				auto& c = entity.GetComponent<LightComponent>();
				if (field == "Color") { out = c.Color; return true; }
				if (field == "Direction") { out = c.Direction; return true; }
			}
			else if (comp == "PBRMaterial" && entity.HasComponent<PBRMaterialComponent>())
			{
				auto& c = entity.GetComponent<PBRMaterialComponent>();
				if (field == "Albedo") { out = c.Albedo; return true; }
				if (field == "Emissive") { out = c.Emissive; return true; }
			}
			else if (comp == "Mesh" && entity.HasComponent<MeshComponent>())
			{
				auto& c = entity.GetComponent<MeshComponent>();
				if (field == "Scale") { out = c.Scale; return true; }
			}
			return false;
		}

		bool SetComponentVector3(Entity& entity, const std::string& comp, const std::string& field, const glm::vec3& value)
		{
			if (comp == "Transform")
			{
				auto& c = entity.GetComponent<TransformComponent>();
				if (field == "Translation") { c.Translation = value; return true; }
				if (field == "Rotation") { c.Rotation = value; return true; }
				if (field == "Scale") { c.Scale = value; return true; }
			}
			else if (comp == "BoxCollider" && entity.HasComponent<BoxColliderComponent>())
			{
				auto& c = entity.GetComponent<BoxColliderComponent>();
				if (field == "Offset") { c.Offset = value; return true; }
				if (field == "Size") { c.Size = value; return true; }
			}
			else if (comp == "SphereCollider" && entity.HasComponent<SphereColliderComponent>())
			{
				auto& c = entity.GetComponent<SphereColliderComponent>();
				if (field == "Offset") { c.Offset = value; return true; }
			}
			else if (comp == "Light" && entity.HasComponent<LightComponent>())
			{
				auto& c = entity.GetComponent<LightComponent>();
				if (field == "Color") { c.Color = value; return true; }
				if (field == "Direction") { c.Direction = value; return true; }
			}
			else if (comp == "PBRMaterial" && entity.HasComponent<PBRMaterialComponent>())
			{
				auto& c = entity.GetComponent<PBRMaterialComponent>();
				if (field == "Albedo") { c.Albedo = value; return true; }
				if (field == "Emissive") { c.Emissive = value; return true; }
			}
			else if (comp == "Mesh" && entity.HasComponent<MeshComponent>())
			{
				auto& c = entity.GetComponent<MeshComponent>();
				if (field == "Scale") { c.Scale = value; return true; }
			}
			return false;
		}

		bool GetComponentFloat(Entity& entity, const std::string& comp, const std::string& field, float& out)
		{
			if (comp == "SphereCollider" && entity.HasComponent<SphereColliderComponent>())
			{
				auto& c = entity.GetComponent<SphereColliderComponent>();
				if (field == "Radius") { out = c.Radius; return true; }
			}
			if (comp == "BoxCollider" && entity.HasComponent<BoxColliderComponent>())
			{
				auto& c = entity.GetComponent<BoxColliderComponent>();
				if (field == "Density") { out = c.Density; return true; }
				if (field == "Friction") { out = c.Friction; return true; }
				if (field == "Restitution") { out = c.Restitution; return true; }
			}
			if (comp == "SphereCollider" && entity.HasComponent<SphereColliderComponent>())
			{
				auto& c = entity.GetComponent<SphereColliderComponent>();
				if (field == "Density") { out = c.Density; return true; }
				if (field == "Friction") { out = c.Friction; return true; }
				if (field == "Restitution") { out = c.Restitution; return true; }
			}
			if (comp == "Light" && entity.HasComponent<LightComponent>())
			{
				auto& c = entity.GetComponent<LightComponent>();
				if (field == "Intensity") { out = c.Intensity; return true; }
			}
			if (comp == "PBRMaterial" && entity.HasComponent<PBRMaterialComponent>())
			{
				auto& c = entity.GetComponent<PBRMaterialComponent>();
				if (field == "Metallic") { out = c.Metallic; return true; }
				if (field == "Roughness") { out = c.Roughness; return true; }
				if (field == "AO") { out = c.AO; return true; }
				if (field == "EmissiveIntensity") { out = c.EmissiveIntensity; return true; }
			}
			if (comp == "Rigidbody" && entity.HasComponent<RigidbodyComponent>())
			{
				auto& c = entity.GetComponent<RigidbodyComponent>();
				if (field == "Type") { out = (float)(int)c.Type; return true; }
			}
			if (comp == "Light" && entity.HasComponent<LightComponent>())
			{
				auto& c = entity.GetComponent<LightComponent>();
				if (field == "Type") { out = (float)(int)c.Type; return true; }
			}
			return false;
		}

		bool SetComponentFloat(Entity& entity, const std::string& comp, const std::string& field, float value)
		{
			if (comp == "SphereCollider" && entity.HasComponent<SphereColliderComponent>())
			{
				auto& c = entity.GetComponent<SphereColliderComponent>();
				if (field == "Radius") { c.Radius = value; return true; }
				if (field == "Density") { c.Density = value; return true; }
				if (field == "Friction") { c.Friction = value; return true; }
				if (field == "Restitution") { c.Restitution = value; return true; }
			}
			if (comp == "BoxCollider" && entity.HasComponent<BoxColliderComponent>())
			{
				auto& c = entity.GetComponent<BoxColliderComponent>();
				if (field == "Density") { c.Density = value; return true; }
				if (field == "Friction") { c.Friction = value; return true; }
				if (field == "Restitution") { c.Restitution = value; return true; }
			}
			if (comp == "Light" && entity.HasComponent<LightComponent>())
			{
				auto& c = entity.GetComponent<LightComponent>();
				if (field == "Intensity") { c.Intensity = value; return true; }
				if (field == "Type") { c.Type = (decltype(c.Type))(int)value; return true; }
			}
			if (comp == "PBRMaterial" && entity.HasComponent<PBRMaterialComponent>())
			{
				auto& c = entity.GetComponent<PBRMaterialComponent>();
				if (field == "Metallic") { c.Metallic = value; return true; }
				if (field == "Roughness") { c.Roughness = value; return true; }
				if (field == "AO") { c.AO = value; return true; }
				if (field == "EmissiveIntensity") { c.EmissiveIntensity = value; return true; }
			}
			if (comp == "Rigidbody" && entity.HasComponent<RigidbodyComponent>())
			{
				auto& c = entity.GetComponent<RigidbodyComponent>();
				if (field == "Type") { c.Type = (RigidbodyComponent::BodyType)(int)value; return true; }
			}
			return false;
		}

		bool GetComponentBool(Entity& entity, const std::string& comp, const std::string& field, bool& out)
		{
			if (comp == "Camera" && entity.HasComponent<CameraComponent>() && field == "Primary") { out = entity.GetComponent<CameraComponent>().Primary; return true; }
			if (comp == "Rigidbody" && entity.HasComponent<RigidbodyComponent>() && field == "FixedRotation") { out = entity.GetComponent<RigidbodyComponent>().FixedRotation; return true; }
			return false;
		}

		bool SetComponentBool(Entity& entity, const std::string& comp, const std::string& field, bool value)
		{
			if (comp == "Camera" && entity.HasComponent<CameraComponent>() && field == "Primary") { entity.GetComponent<CameraComponent>().Primary = value; return true; }
			if (comp == "Rigidbody" && entity.HasComponent<RigidbodyComponent>() && field == "FixedRotation") { entity.GetComponent<RigidbodyComponent>().FixedRotation = value; return true; }
			return false;
		}

		bool GetComponentString(Entity& entity, const std::string& comp, const std::string& field, std::string& out)
		{
			if (comp == "Script" && entity.HasComponent<ScriptComponent>() && field == "ClassName") { out = entity.GetComponent<ScriptComponent>().ClassName; return true; }
			if (comp == "Mesh" && entity.HasComponent<MeshComponent>() && field == "ModelPath") { out = entity.GetComponent<MeshComponent>().ModelPath; return true; }
			if (comp == "Tag" && field == "Tag") { out = entity.GetComponent<TagComponent>().Tag; return true; }
			return false;
		}

		bool SetComponentString(Entity& entity, const std::string& comp, const std::string& field, const std::string& value)
		{
			if (comp == "Script" && entity.HasComponent<ScriptComponent>() && field == "ClassName") { entity.GetComponent<ScriptComponent>().ClassName = value; return true; }
			if (comp == "Mesh" && entity.HasComponent<MeshComponent>() && field == "ModelPath") { entity.GetComponent<MeshComponent>().ModelPath = value; return true; }
			if (comp == "Tag" && field == "Tag") { entity.GetComponent<TagComponent>().Tag = value; return true; }
			return false;
		}

		bool EntityHasNamedComponent(Entity& entity, const std::string& comp)
		{
			if (comp == "Transform") return entity.HasComponent<TransformComponent>();
			if (comp == "Camera") return entity.HasComponent<CameraComponent>();
			if (comp == "Script") return entity.HasComponent<ScriptComponent>();
			if (comp == "Rigidbody") return entity.HasComponent<RigidbodyComponent>();
			if (comp == "BoxCollider") return entity.HasComponent<BoxColliderComponent>();
			if (comp == "SphereCollider") return entity.HasComponent<SphereColliderComponent>();
			if (comp == "Light") return entity.HasComponent<LightComponent>();
			if (comp == "PBRMaterial") return entity.HasComponent<PBRMaterialComponent>();
			if (comp == "Mesh") return entity.HasComponent<MeshComponent>();
			return false;
		}

		bool AddNamedComponent(Entity& entity, const std::string& comp)
		{
			if (entity.HasComponent<TransformComponent>() && comp == "Transform") return false;

			if (comp == "Camera" && !entity.HasComponent<CameraComponent>()) { entity.AddComponent<CameraComponent>(); return true; }
			if (comp == "Script" && !entity.HasComponent<ScriptComponent>()) { entity.AddComponent<ScriptComponent>(); return true; }
			if (comp == "Rigidbody" && !entity.HasComponent<RigidbodyComponent>()) { entity.AddComponent<RigidbodyComponent>(); return true; }
			if (comp == "BoxCollider" && !entity.HasComponent<BoxColliderComponent>()) { entity.AddComponent<BoxColliderComponent>(); return true; }
			if (comp == "SphereCollider" && !entity.HasComponent<SphereColliderComponent>()) { entity.AddComponent<SphereColliderComponent>(); return true; }
			if (comp == "Light" && !entity.HasComponent<LightComponent>()) { entity.AddComponent<LightComponent>(); return true; }
			if (comp == "PBRMaterial" && !entity.HasComponent<PBRMaterialComponent>()) { entity.AddComponent<PBRMaterialComponent>(); return true; }
			if (comp == "Mesh" && !entity.HasComponent<MeshComponent>()) { entity.AddComponent<MeshComponent>(); return true; }
			return false;
		}

		bool RemoveNamedComponent(Entity& entity, const std::string& comp)
		{
			if (comp == "Camera" && entity.HasComponent<CameraComponent>()) { entity.RemoveComponent<CameraComponent>(); return true; }
			if (comp == "Script" && entity.HasComponent<ScriptComponent>()) { entity.RemoveComponent<ScriptComponent>(); return true; }
			if (comp == "Rigidbody" && entity.HasComponent<RigidbodyComponent>()) { entity.RemoveComponent<RigidbodyComponent>(); return true; }
			if (comp == "BoxCollider" && entity.HasComponent<BoxColliderComponent>()) { entity.RemoveComponent<BoxColliderComponent>(); return true; }
			if (comp == "SphereCollider" && entity.HasComponent<SphereColliderComponent>()) { entity.RemoveComponent<SphereColliderComponent>(); return true; }
			if (comp == "Light" && entity.HasComponent<LightComponent>()) { entity.RemoveComponent<LightComponent>(); return true; }
			if (comp == "PBRMaterial" && entity.HasComponent<PBRMaterialComponent>()) { entity.RemoveComponent<PBRMaterialComponent>(); return true; }
			if (comp == "Mesh" && entity.HasComponent<MeshComponent>()) { entity.RemoveComponent<MeshComponent>(); return true; }
			return false;
		}

		// --- Entity Lua methods ----------------------------------------------

		int LuaEntity_GetName(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return PushNilEntity(L);

			lua_pushstring(L, entity->GetName().c_str());
			return 1;
		}

		int LuaEntity_SetName(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			entity->GetComponent<TagComponent>().Tag = SafeString(L, 2, "Entity");
			return 0;
		}

		int LuaEntity_GetPosition(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			auto& tc = entity->GetComponent<TransformComponent>();
			lua_pushnumber(L, tc.Translation.x);
			lua_pushnumber(L, tc.Translation.y);
			lua_pushnumber(L, tc.Translation.z);
			return 3;
		}

		int LuaEntity_SetPosition(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			auto& tc = entity->GetComponent<TransformComponent>();
			tc.Translation = glm::vec3(
				(float)luaL_checknumber(L, 2),
				(float)luaL_checknumber(L, 3),
				(float)luaL_checknumber(L, 4));
			return 0;
		}

		int LuaEntity_GetRotation(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			auto& tc = entity->GetComponent<TransformComponent>();
			lua_pushnumber(L, tc.Rotation.x);
			lua_pushnumber(L, tc.Rotation.y);
			lua_pushnumber(L, tc.Rotation.z);
			return 3;
		}

		int LuaEntity_SetRotation(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			auto& tc = entity->GetComponent<TransformComponent>();
			tc.Rotation = glm::vec3(
				(float)luaL_checknumber(L, 2),
				(float)luaL_checknumber(L, 3),
				(float)luaL_checknumber(L, 4));
			return 0;
		}

		int LuaEntity_GetScale(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			auto& tc = entity->GetComponent<TransformComponent>();
			lua_pushnumber(L, tc.Scale.x);
			lua_pushnumber(L, tc.Scale.y);
			lua_pushnumber(L, tc.Scale.z);
			return 3;
		}

		int LuaEntity_SetScale(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			auto& tc = entity->GetComponent<TransformComponent>();
			tc.Scale = glm::vec3(
				(float)luaL_checknumber(L, 2),
				(float)luaL_checknumber(L, 3),
				(float)luaL_checknumber(L, 4));
			return 0;
		}

		int LuaEntity_HasComponent(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
			{
				lua_pushboolean(L, 0);
				return 1;
			}

			lua_pushboolean(L, EntityHasNamedComponent(*entity, SafeString(L, 2)) ? 1 : 0);
			return 1;
		}

		int LuaEntity_AddComponent(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
			{
				lua_pushboolean(L, 0);
				return 1;
			}

			std::string comp = SafeString(L, 2);
			bool added = AddNamedComponent(*entity, comp);
			if (!added && !EntityHasNamedComponent(*entity, comp))
				FG_CORE_ERROR("AddComponent: unknown component '{0}'", comp);

			lua_pushboolean(L, added ? 1 : 0);
			return 1;
		}

		int LuaEntity_RemoveComponent(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			RemoveNamedComponent(*entity, SafeString(L, 2));
			return 0;
		}

		int LuaEntity_GetFloat(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			float value = 0.0f;
			if (!entity || !(*entity) || !GetComponentFloat(*entity, SafeString(L, 2), SafeString(L, 3), value))
			{
				lua_pushnil(L);
				return 1;
			}

			lua_pushnumber(L, value);
			return 1;
		}

		int LuaEntity_SetFloat(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			SetComponentFloat(*entity, SafeString(L, 2), SafeString(L, 3), (float)luaL_checknumber(L, 4));
			return 0;
		}

		int LuaEntity_GetBool(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			bool value = false;
			if (!entity || !(*entity) || !GetComponentBool(*entity, SafeString(L, 2), SafeString(L, 3), value))
			{
				lua_pushnil(L);
				return 1;
			}

			lua_pushboolean(L, value ? 1 : 0);
			return 1;
		}

		int LuaEntity_SetBool(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			SetComponentBool(*entity, SafeString(L, 2), SafeString(L, 3), lua_toboolean(L, 4) != 0);
			return 0;
		}

		int LuaEntity_GetString(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			std::string value;
			if (!entity || !(*entity) || !GetComponentString(*entity, SafeString(L, 2), SafeString(L, 3), value))
			{
				lua_pushnil(L);
				return 1;
			}

			lua_pushlstring(L, value.c_str(), value.size());
			return 1;
		}

		int LuaEntity_SetString(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			SetComponentString(*entity, SafeString(L, 2), SafeString(L, 3), SafeString(L, 4));
			return 0;
		}

		int LuaEntity_GetVector3(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			glm::vec3 value(0.0f);
			if (!entity || !(*entity) || !GetComponentVector3(*entity, SafeString(L, 2), SafeString(L, 3), value))
				return 0;

			lua_pushnumber(L, value.x);
			lua_pushnumber(L, value.y);
			lua_pushnumber(L, value.z);
			return 3;
		}

		int LuaEntity_SetVector3(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			glm::vec3 value(
				(float)luaL_checknumber(L, 4),
				(float)luaL_checknumber(L, 5),
				(float)luaL_checknumber(L, 6));
			SetComponentVector3(*entity, SafeString(L, 2), SafeString(L, 3), value);
			return 0;
		}

		int LuaEntity_Destroy(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			Scene* scene = ScriptEngine::GetActiveScene();
			if (scene)
				scene->DestroyEntity(*entity);

			return 0;
		}

		int LuaEntity_Duplicate(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return PushNilEntity(L);

			Scene* scene = ScriptEngine::GetActiveScene();
			if (!scene)
				return PushNilEntity(L);

			return PushEntity(L, scene->DuplicateEntity(*entity));
		}

		// --- Scene Lua methods -----------------------------------------------

		int LuaScene_CreateEntity(lua_State* L)
		{
			Scene* scene = GetSceneArg(L);
			if (!scene)
				return PushNilEntity(L);

			// Colon call: Scene:CreateEntity(name). Dot call: Scene.CreateEntity(name).
			std::string name = TestScene(L, 1) ? SafeString(L, 2, "Entity") : SafeString(L, 1, "Entity");
			return PushEntity(L, scene->CreateEntity(name));
		}

		int LuaScene_DestroyEntity(lua_State* L)
		{
			Scene* scene = GetSceneArg(L);
			if (!scene)
				return 0;

			Entity* entity = TestEntity(L, TestScene(L, 1) ? 2 : 1);
			if (entity && *entity)
				scene->DestroyEntity(*entity);
			return 0;
		}

		int LuaScene_GetEntityByName(lua_State* L)
		{
			Scene* scene = GetSceneArg(L);
			if (!scene)
				return PushNilEntity(L);

			const char* name = TestScene(L, 1) ? SafeString(L, 2, "") : SafeString(L, 1, "");
			Entity entity = scene->FindEntityByName(name);
			if (!entity)
				return PushNilEntity(L);

			return PushEntity(L, entity);
		}

		int LuaScene_GetEntitiesWithComponent(lua_State* L)
		{
			Scene* scene = GetSceneArg(L);
			if (!scene)
			{
				lua_newtable(L);
				return 1;
			}

			std::string comp = TestScene(L, 1) ? SafeString(L, 2, "") : SafeString(L, 1, "");
			lua_newtable(L);
			int index = 0;

			auto pushIf = [&](auto view) {
				for (auto e : view)
				{
					Entity entity{ e, scene };
					lua_pushinteger(L, ++index);
					PushEntity(L, entity);
					lua_settable(L, -3);
				}
			};

			if (comp == "Transform") pushIf(scene->GetAllEntitiesWith<TransformComponent>());
			else if (comp == "Rigidbody") pushIf(scene->GetAllEntitiesWith<RigidbodyComponent>());
			else if (comp == "BoxCollider") pushIf(scene->GetAllEntitiesWith<BoxColliderComponent>());
			else if (comp == "SphereCollider") pushIf(scene->GetAllEntitiesWith<SphereColliderComponent>());
			else if (comp == "Camera") pushIf(scene->GetAllEntitiesWith<CameraComponent>());
			else if (comp == "Script") pushIf(scene->GetAllEntitiesWith<ScriptComponent>());
			else if (comp == "Light") pushIf(scene->GetAllEntitiesWith<LightComponent>());
			else if (comp == "PBRMaterial") pushIf(scene->GetAllEntitiesWith<PBRMaterialComponent>());
			else if (comp == "Mesh") pushIf(scene->GetAllEntitiesWith<MeshComponent>());
			else if (comp == "Tag") pushIf(scene->GetAllEntitiesWith<TagComponent>());

			return 1;
		}

		// --- Global functions -------------------------------------------------

		int LuaSpawnPrefab(lua_State* L)
		{
			Scene* scene = ScriptEngine::GetActiveScene();
			std::string prefabName = SafeString(L, 1);
			if (!scene || prefabName.empty())
				return PushNilEntity(L);

			Ref<Prefab> prefab = PrefabLibrary::Get(prefabName);
			if (!prefab)
				return PushNilEntity(L);

			bool usePosition = lua_gettop(L) >= 4;
			glm::vec3 position(
				(float)luaL_optnumber(L, 2, 0.0),
				(float)luaL_optnumber(L, 3, 0.0),
				(float)luaL_optnumber(L, 4, 0.0));

			return PushEntity(L, prefab->Instantiate(scene, usePosition, position));
		}

		int LuaGetTime(lua_State* L)
		{
			lua_pushnumber(L, (double)glfwGetTime());
			return 1;
		}

		int LuaLog(lua_State* L)
		{
			FG_CORE_INFO("[Lua] {0}", SafeString(L, 1));
			return 0;
		}

	}

	void ScriptEngine::Init()
	{
		if (s_LuaState)
			return;

		s_LuaState = luaL_newstate();
		if (!s_LuaState)
		{
			FG_CORE_ERROR("Failed to create Lua state");
			return;
		}

		luaL_openlibs(s_LuaState);
		RegisterBindings();

		FG_CORE_INFO("ScriptEngine initialized");
	}

	void ScriptEngine::Shutdown()
	{
		if (s_LuaState)
		{
			lua_close(s_LuaState);
			s_LuaState = nullptr;
		}

		s_EntityScripts.clear();
		s_ScriptSourceCache.clear();
		s_ActiveScene = nullptr;

		FG_CORE_INFO("ScriptEngine shut down");
	}

	void ScriptEngine::RegisterBindings()
	{
		lua_State* L = s_LuaState;

		// Global functions
		lua_register(L, "GetTime", LuaGetTime);
		lua_register(L, "Log", LuaLog);
		lua_register(L, "SpawnPrefab", LuaSpawnPrefab);

		// Entity metatable
		luaL_newmetatable(L, kEntityMeta);
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
		luaL_Reg entityMethods[] = {
			{ "GetName", LuaEntity_GetName },
			{ "SetName", LuaEntity_SetName },
			{ "GetPosition", LuaEntity_GetPosition },
			{ "SetPosition", LuaEntity_SetPosition },
			{ "GetRotation", LuaEntity_GetRotation },
			{ "SetRotation", LuaEntity_SetRotation },
			{ "GetScale", LuaEntity_GetScale },
			{ "SetScale", LuaEntity_SetScale },
			{ "HasComponent", LuaEntity_HasComponent },
			{ "AddComponent", LuaEntity_AddComponent },
			{ "RemoveComponent", LuaEntity_RemoveComponent },
			{ "GetFloat", LuaEntity_GetFloat },
			{ "SetFloat", LuaEntity_SetFloat },
			{ "GetBool", LuaEntity_GetBool },
			{ "SetBool", LuaEntity_SetBool },
			{ "GetString", LuaEntity_GetString },
			{ "SetString", LuaEntity_SetString },
			{ "GetVector3", LuaEntity_GetVector3 },
			{ "SetVector3", LuaEntity_SetVector3 },
			{ "Destroy", LuaEntity_Destroy },
			{ "Duplicate", LuaEntity_Duplicate },
			{ nullptr, nullptr }
		};
		luaL_setfuncs(L, entityMethods, 0);
		lua_pop(L, 1);

		// Scene metatable
		luaL_newmetatable(L, kSceneMeta);
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");
		luaL_Reg sceneMethods[] = {
			{ "CreateEntity", LuaScene_CreateEntity },
			{ "DestroyEntity", LuaScene_DestroyEntity },
			{ "GetEntityByName", LuaScene_GetEntityByName },
			{ "GetEntitiesWithComponent", LuaScene_GetEntitiesWithComponent },
			{ nullptr, nullptr }
		};
		luaL_setfuncs(L, sceneMethods, 0);
		lua_pop(L, 1);
	}

	void ScriptEngine::OnRuntimeStart(Scene* scene)
	{
		if (!s_LuaState)
			Init();

		s_ActiveScene = scene;
		s_EntityScripts.clear();

		if (!s_LuaState || !scene)
			return;

		// Expose the scene to scripts as the global `Scene`. The userdata holds
		// a Scene* so the scene itself stays owned by the application.
		{
			void* storage = lua_newuserdatauv(s_LuaState, sizeof(Scene*), 0);
			*static_cast<Scene**>(storage) = scene;
			luaL_setmetatable(s_LuaState, kSceneMeta);
			lua_setglobal(s_LuaState, "Scene");
		}

		auto view = scene->GetAllEntitiesWith<ScriptComponent>();
		for (auto e : view)
		{
			Entity entity{ e, scene };
			auto& sc = entity.GetComponent<ScriptComponent>();

			std::string directory = s_ScriptDirectory;
			if (!directory.empty() && directory.back() != '/' && directory.back() != '\\')
				directory += '/';

			std::string filepath = directory + sc.ClassName + ".lua";
			std::string error;
			if (!LoadScriptForEntity(entity, filepath, &error))
			{
				FG_CORE_ERROR("Failed to load script for entity '{0}': {1}", entity.GetName(), error);
				continue;
			}

			CallEntityFunction(entity, "OnCreate");
		}
	}

	void ScriptEngine::OnRuntimeStop()
	{
		if (!s_LuaState)
			return;

		// Tear down every script instance.
		for (auto& [id, ref] : s_EntityScripts)
		{
			(void)id;
			luaL_unref(s_LuaState, LUA_REGISTRYINDEX, ref);
		}
		s_EntityScripts.clear();
		s_ActiveScene = nullptr;

		lua_pushnil(s_LuaState);
		lua_setglobal(s_LuaState, "Scene");

		FG_CORE_INFO("ScriptEngine runtime stop");
	}

	void ScriptEngine::OnCreateEntity(Entity entity)
	{
		CallEntityFunction(entity, "OnCreate");
	}

	void ScriptEngine::OnUpdateEntity(Entity entity, Timestep ts)
	{
		CallEntityFunction(entity, "OnUpdate", ts.GetSeconds());
	}

	void ScriptEngine::OnDestroyEntity(Entity entity)
	{
		CallEntityFunction(entity, "OnDestroy");
		ReleaseEntityScript((uint32_t)(entt::entity)entity);
	}

	bool ScriptEngine::HasScript(Entity entity)
	{
		return s_EntityScripts.find((uint32_t)(entt::entity)entity) != s_EntityScripts.end();
	}

	bool ScriptEngine::ExecuteString(const std::string& code, std::string* errorOut)
	{
		if (!s_LuaState)
			Init();

		if (luaL_dostring(s_LuaState, code.c_str()) != 0)
		{
			if (errorOut)
				*errorOut = SafeString(s_LuaState, -1, "unknown error");
			lua_pop(s_LuaState, 1);
			return false;
		}

		return true;
	}

	bool ScriptEngine::LoadScriptForEntity(Entity entity, const std::string& filepath, std::string* errorOut)
	{
		if (!s_LuaState)
			Init();
		if (!s_LuaState || !entity)
			return false;

		// Cache file contents by path to avoid redundant disk reads.
		auto cacheIt = s_ScriptSourceCache.find(filepath);
		std::string source;
		if (cacheIt != s_ScriptSourceCache.end())
		{
			source = cacheIt->second;
		}
		else
		{
			std::ifstream file(filepath);
			if (!file.is_open())
			{
				if (errorOut)
					*errorOut = "could not open " + filepath;
				return false;
			}

			std::stringstream ss;
			ss << file.rdbuf();
			source = ss.str();
			s_ScriptSourceCache[filepath] = source;
		}

		// Each entity gets its own execution of the chunk so scripts keep their
		// own upvalues/state.
		if (luaL_loadbuffer(s_LuaState, source.c_str(), source.size(), filepath.c_str()) != 0)
		{
			if (errorOut)
				*errorOut = SafeString(s_LuaState, -1, "compile error");
			lua_pop(s_LuaState, 1);
			return false;
		}

		if (lua_pcall(s_LuaState, 0, 1, 0) != 0)
		{
			if (errorOut)
				*errorOut = SafeString(s_LuaState, -1, "runtime error");
			lua_pop(s_LuaState, 1);
			return false;
		}

		if (!lua_istable(s_LuaState, -1))
		{
			if (errorOut)
				*errorOut = "script did not return a table";
			lua_pop(s_LuaState, 1);
			return false;
		}

		ReleaseEntityScript((uint32_t)(entt::entity)entity);
		int ref = luaL_ref(s_LuaState, LUA_REGISTRYINDEX);
		s_EntityScripts[(uint32_t)(entt::entity)entity] = ref;
		return true;
	}

	bool ScriptEngine::CallEntityFunction(Entity entity, const char* functionName, float dt)
	{
		if (!s_LuaState || !entity)
			return false;

		auto it = s_EntityScripts.find((uint32_t)(entt::entity)entity);
		if (it == s_EntityScripts.end())
			return false;

		lua_State* L = s_LuaState;

		lua_rawgeti(L, LUA_REGISTRYINDEX, it->second); // script table
		if (!lua_istable(L, -1))
		{
			lua_pop(L, 1);
			return false;
		}

		lua_getfield(L, -1, functionName);
		if (!lua_isfunction(L, -1))
		{
			lua_pop(L, 2);
			return false;
		}

		PushEntity(L, entity);
		int argCount = 1;
		if (std::string(functionName) == "OnUpdate")
		{
			lua_pushnumber(L, dt);
			argCount = 2;
		}

		bool ok = true;
		if (lua_pcall(L, argCount, 0, 0) != 0)
		{
			FG_CORE_ERROR("Lua error in {0}: {1}", functionName, lua_tostring(L, -1));
			lua_pop(L, 1);
			ok = false;
		}

		lua_pop(L, 1); // script table
		return ok;
	}

	std::string ScriptEngine::GetGlobalAsString(const std::string& name)
	{
		if (!s_LuaState)
			return {};

		lua_getglobal(s_LuaState, name.c_str());
		std::string result = SafeString(s_LuaState, -1, "nil");
		lua_pop(s_LuaState, 1);
		return result;
	}

	void ScriptEngine::ReleaseEntityScript(uint32_t entityID)
	{
		auto it = s_EntityScripts.find(entityID);
		if (it == s_EntityScripts.end())
			return;

		if (s_LuaState)
			luaL_unref(s_LuaState, LUA_REGISTRYINDEX, it->second);
		s_EntityScripts.erase(it);
	}

}
