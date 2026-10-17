#include "fgpch.h"
#include "Forge/Scripting/ScriptEngine.h"
#include "Forge/Audio/AudioEngine.h"
#include "Forge/Core/Time.h"
#include "Forge/Core/FileSystem.h"

#include "Forge/Scene/Scene.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Prefab.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <fstream>
#include <set>
#include <sstream>

namespace Forge {

	lua_State* ScriptEngine::s_LuaState = nullptr;
	Scene* ScriptEngine::s_ActiveScene = nullptr;
	std::string ScriptEngine::s_ScriptDirectory = "resources/scripts/";
	std::unordered_map<uint32_t, int> ScriptEngine::s_EntityScripts;
	std::unordered_map<uint32_t, std::string> ScriptEngine::s_EntityScriptPaths;
	std::unordered_map<uint32_t, double> ScriptEngine::s_EntityDeltaTimes;
	double ScriptEngine::s_LastDeltaTime = 0.0;
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

		// Pushes a boolean and returns 1, the usual way a Lua binding finishes.
		int PushBool(lua_State* L, bool value)
		{
			lua_pushboolean(L, value ? 1 : 0);
			return 1;
		}

		// Reports a failed component property write: names what was not found,
		// and pushes false.
		//
		// A mistyped component or field used to be discarded without a sound, so
		// entity:SetFloat("Rigidbody", "Masa", 5) looked exactly like it worked.
		// Returning a boolean lets a script check; logging names the mistake,
		// because a silent no-op is the failure mode that costs the longest to
		// find. AddComponent already reported this way; these setters did not.
		int PushSetFailure(lua_State* L, const char* function,
			const std::string& component, const std::string& field)
		{
			FG_CORE_ERROR("entity:{0}: no component field '{1}.{2}'", function, component, field);
			return PushBool(L, false);
		}

		int PushMutationFailure(lua_State* L, const char* function, const char* reason)
		{
			FG_CORE_ERROR("entity:{0}: {1}", function, reason);
			return PushBool(L, false);
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
			if (comp == "Audio" && entity.HasComponent<AudioSourceComponent>())
			{
				auto& c = entity.GetComponent<AudioSourceComponent>();
				if (field == "Volume") { c.Volume = value; return true; }
				if (field == "Pitch") { c.Pitch = value; return true; }
			}
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
			if (comp == "Audio" && entity.HasComponent<AudioSourceComponent>() && field == "Loop") { out = entity.GetComponent<AudioSourceComponent>().Loop; return true; }
			if (comp == "Audio" && entity.HasComponent<AudioSourceComponent>() && field == "PlayOnStart") { out = entity.GetComponent<AudioSourceComponent>().PlayOnStart; return true; }
			return false;
		}

		bool SetComponentBool(Entity& entity, const std::string& comp, const std::string& field, bool value)
		{
			if (comp == "Camera" && entity.HasComponent<CameraComponent>() && field == "Primary") { entity.GetComponent<CameraComponent>().Primary = value; return true; }
			if (comp == "Audio" && entity.HasComponent<AudioSourceComponent>() && field == "Loop") { entity.GetComponent<AudioSourceComponent>().Loop = value; return true; }
			if (comp == "Audio" && entity.HasComponent<AudioSourceComponent>() && field == "PlayOnStart") { entity.GetComponent<AudioSourceComponent>().PlayOnStart = value; return true; }
			if (comp == "Rigidbody" && entity.HasComponent<RigidbodyComponent>() && field == "FixedRotation") { entity.GetComponent<RigidbodyComponent>().FixedRotation = value; return true; }
			return false;
		}

		bool GetComponentString(Entity& entity, const std::string& comp, const std::string& field, std::string& out)
		{
			if (comp == "Script" && entity.HasComponent<ScriptComponent>() && field == "ClassName") { out = entity.GetComponent<ScriptComponent>().ClassName; return true; }
			if (comp == "Mesh" && entity.HasComponent<MeshComponent>() && field == "ModelPath") { out = entity.GetComponent<MeshComponent>().ModelPath; return true; }
			if (comp == "Audio" && entity.HasComponent<AudioSourceComponent>() && field == "Volume") { out = entity.GetComponent<AudioSourceComponent>().Volume; return true; }
			if (comp == "Audio" && entity.HasComponent<AudioSourceComponent>() && field == "Pitch") { out = entity.GetComponent<AudioSourceComponent>().Pitch; return true; }
			if (comp == "Tag" && field == "Tag") { out = entity.GetComponent<TagComponent>().Tag; return true; }
			return false;
		}

		bool SetComponentString(Entity& entity, const std::string& comp, const std::string& field, const std::string& value)
		{
			if (comp == "Script" && entity.HasComponent<ScriptComponent>() && field == "ClassName") { entity.GetComponent<ScriptComponent>().ClassName = value; return true; }
			if (comp == "Audio" && entity.HasComponent<AudioSourceComponent>() && field == "SourcePath") { entity.GetComponent<AudioSourceComponent>().SourcePath = value; return true; }
			if (comp == "Audio" && entity.HasComponent<AudioSourceComponent>() && field == "Bus")
			{
				Forge::AudioBus bus = entity.GetComponent<AudioSourceComponent>().Bus;
				if (ParseAudioBus(value, bus))
				{
					entity.GetComponent<AudioSourceComponent>().Bus = bus;
					return true;
				}
				return false;   // unknown bus name: reject rather than ignore
			}
			if (comp == "Mesh" && entity.HasComponent<MeshComponent>() && field == "ModelPath") {
				entity.GetComponent<MeshComponent>().ModelPath = value;

				// Importing here is what stops a script from looking like it
				// worked: the path is what the entity renders, and a script that
				// set one left the old geometry in place.
				if (Scene* scene = entity.GetScene(); scene && !scene->ImportMeshData(entity))
				{
					FG_CORE_ERROR("entity:SetString: could not import mesh model '{0}'", value);
					return false;
				}
				return true;
			}
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
			if (comp == "Audio") return entity.HasComponent<AudioSourceComponent>();
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
			if (comp == "Audio" && !entity.HasComponent<AudioSourceComponent>()) { entity.AddComponent<AudioSourceComponent>(); return true; }
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
			if (comp == "Audio" && entity.HasComponent<AudioSourceComponent>()) { entity.RemoveComponent<AudioSourceComponent>(); return true; }
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
				return PushMutationFailure(L, "RemoveComponent", "invalid entity");

			const std::string comp = SafeString(L, 2);
			if (!RemoveNamedComponent(*entity, comp))
			{
				// Covers both an unknown name and a name the entity does not
				// currently carry; the script cannot tell them apart, so the
				// message says what it could not remove.
				return PushMutationFailure(L, "RemoveComponent",
					("entity has no removable component '" + comp + "'").c_str());
			}

			return PushBool(L, true);
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
				return PushMutationFailure(L, "SetFloat", "invalid entity");

			const std::string comp = SafeString(L, 2);
			const std::string field = SafeString(L, 3);

			if (!SetComponentFloat(*entity, comp, field, (float)luaL_checknumber(L, 4)))
				return PushSetFailure(L, "SetFloat", comp, field);

			return PushBool(L, true);
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
				return PushMutationFailure(L, "SetBool", "invalid entity");

			const std::string comp = SafeString(L, 2);
			const std::string field = SafeString(L, 3);

			if (!SetComponentBool(*entity, comp, field, lua_toboolean(L, 4) != 0))
				return PushSetFailure(L, "SetBool", comp, field);

			return PushBool(L, true);
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
				return PushMutationFailure(L, "SetString", "invalid entity");

			const std::string comp = SafeString(L, 2);
			const std::string field = SafeString(L, 3);

			if (!SetComponentString(*entity, comp, field, SafeString(L, 4)))
				return PushSetFailure(L, "SetString", comp, field);

			return PushBool(L, true);
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
				return PushMutationFailure(L, "SetVector3", "invalid entity");

			glm::vec3 value(
				(float)luaL_checknumber(L, 4),
				(float)luaL_checknumber(L, 5),
				(float)luaL_checknumber(L, 6));

			const std::string comp = SafeString(L, 2);
			const std::string field = SafeString(L, 3);

			if (!SetComponentVector3(*entity, comp, field, value))
				return PushSetFailure(L, "SetVector3", comp, field);

			return PushBool(L, true);
		}

		int LuaEntity_Destroy(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !(*entity))
				return 0;

			Scene* scene = ScriptEngine::GetActiveScene();
			if (!scene)
				return PushMutationFailure(L, "Destroy", "no active scene");

			scene->DestroyEntity(*entity);
			return PushBool(L, true);
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
			else if (comp == "Audio") pushIf(scene->GetAllEntitiesWith<AudioSourceComponent>());
			else if (comp == "Tag") pushIf(scene->GetAllEntitiesWith<TagComponent>());

			return 1;
		}

		// PlaySound(path, volume, pitch, loop) -> boolean
		//
		// Returns false when no audio device is available or the path is bad, so
		// a script can branch on it rather than assuming success.
		int LuaPlaySound(lua_State* L)
		{
			const std::string path = SafeString(L, 1);
			if (path.empty())
			{
				lua_pushboolean(L, 0);
				return 1;
			}

			const float volume = (float)luaL_optnumber(L, 2, 1.0);
			const float pitch = (float)luaL_optnumber(L, 3, 1.0);
			const bool loop = lua_toboolean(L, 4) != 0;

			// Optional fifth argument routes the sound through a named bus.
			Forge::AudioBus bus = Forge::AudioBus::SoundEffects;
			if (lua_gettop(L) >= 5)
			{
				const std::string busName = SafeString(L, 5);
				if (!busName.empty() && !ParseAudioBus(busName, bus))
				{
					// Unknown bus: report failure instead of guessing.
					lua_pushboolean(L, 0);
					lua_pushnumber(L, 0);
					return 2;
				}
			}

			AudioEngine::SoundHandle handle = AudioEngine::InvalidSound;
			const bool started = AudioEngine::PlaySoundOnBus(path, bus, volume, pitch, loop, &handle);

			lua_pushboolean(L, started ? 1 : 0);
			// Second return value is the handle, for StopSound.
			lua_pushnumber(L, (lua_Number)handle);
			return 2;
		}

		// SetBusVolume(name, volume) -> boolean
		//
		// Ducking a bus is one call regardless of how many sounds it holds:
		//   SetBusVolume("Music", 0.3)
		int LuaSetBusVolume(lua_State* L)
		{
			const std::string busName = SafeString(L, 1);
			const float volume = (float)luaL_optnumber(L, 2, 1.0);

			// An unknown bus name returns false rather than silently succeeding,
			// so a typo in a script is visible.
			lua_pushboolean(L, AudioEngine::SetBusVolume(busName, volume) ? 1 : 0);
			return 1;
		}

		// GetBusVolume(name) -> number
		int LuaGetBusVolume(lua_State* L)
		{
			const std::string busName = SafeString(L, 1);
			Forge::AudioBus bus = Forge::AudioBus::SoundEffects;

			if (!ParseAudioBus(busName, bus))
			{
				lua_pushnil(L);
				return 1;
			}

			lua_pushnumber(L, AudioEngine::GetBusVolume(bus));
			return 1;
		}

		// StopSound(handle) -> boolean
		int LuaStopSound(lua_State* L)
		{
			const lua_Number raw = luaL_optnumber(L, 1, 0.0);
			const AudioEngine::SoundHandle handle = (AudioEngine::SoundHandle)raw;

			lua_pushboolean(L, AudioEngine::StopSound(handle) ? 1 : 0);
			return 1;
		}

		// PlaySound attaches to an AudioSourceComponent instead of playing
		// fire-and-forget, so the scene can reclaim it when it finishes.
		int LuaEntity_PlaySound(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			const std::string path = SafeString(L, 2);
			if (!entity || !*entity)
				return PushNilEntity(L);

			const float volume = (float)luaL_optnumber(L, 3, 1.0);
			const float pitch = (float)luaL_optnumber(L, 4, 1.0);
			const bool loop = lua_toboolean(L, 5) != 0;

			if (!entity->HasComponent<AudioSourceComponent>())
			{
				auto& audio = entity->AddComponent<AudioSourceComponent>();
				audio.SourcePath = path;
				audio.Volume = volume;
				audio.Pitch = pitch;
				audio.Loop = loop;

				// Adding it while simulating starts playback immediately.
				if (!AudioEngine::IsSoundPlaying(audio.SoundHandle))
				{
					AudioEngine::SoundHandle handle = AudioEngine::InvalidSound;
					if (AudioEngine::PlaySound(path, volume, pitch, loop, &handle))
						audio.SoundHandle = handle;
				}

				return PushEntity(L, *entity);
			}

			auto& audio = entity->GetComponent<AudioSourceComponent>();
			audio.SourcePath = path;
			audio.Volume = volume;
			audio.Pitch = pitch;
			audio.Loop = loop;

			if (audio.SoundHandle != AudioEngine::InvalidSound)
				(void)AudioEngine::StopSound(audio.SoundHandle);

			AudioEngine::SoundHandle handle = AudioEngine::InvalidSound;
			if (AudioEngine::PlaySound(path, volume, pitch, loop, &handle))
				audio.SoundHandle = handle;

			return PushEntity(L, *entity);
		}

		int LuaEntity_StopSound(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);
			if (!entity || !*entity)
			{
				lua_pushboolean(L, 0);
				return 1;
			}

			if (!entity->HasComponent<AudioSourceComponent>())
			{
				lua_pushboolean(L, 0);
				return 1;
			}

			auto& audio = entity->GetComponent<AudioSourceComponent>();
			const bool stopped = AudioEngine::StopSound(audio.SoundHandle);
			audio.SoundHandle = AudioEngine::InvalidSound;
			audio.Finished = true;

			lua_pushboolean(L, stopped ? 1 : 0);
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
			lua_pushnumber(L, Time::GetTime());
			return 1;
		}

		// GetDeltaTime([entity]) - the delta time of the frame the optional entity
		// was last updated in, in seconds.
		//
		// AGENTS.md documented this and no such binding existed, so a script
		// calling it got "attempt to call a nil value".
		//
		// Without an argument it reports the most recent delta of any entity,
		// which is the frame the script is being ticked in. Per-entity rather
		// than a single global, because two entities updated in one frame share a
		// dt but an entity that is not being updated must not report a stale
		// value as if it were current.
		int LuaGetDeltaTime(lua_State* L)
		{
			Entity* entity = TestEntity(L, 1);

			if (entity && *entity)
			{
				lua_pushnumber(L, Forge::ScriptEngine::GetEntityDeltaTime(*entity));
				return 1;
			}

			// No entity argument, or it was not one: the current frame's delta.
			lua_pushnumber(L, Forge::ScriptEngine::GetLastDeltaTime());
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
		s_EntityScriptPaths.clear();
		s_ScriptSourceCache.clear();
		s_ActiveScene = nullptr;

		FG_CORE_INFO("ScriptEngine shut down");
	}

	void ScriptEngine::RegisterBindings()
	{
		lua_State* L = s_LuaState;

		// Global functions
		lua_register(L, "GetTime", LuaGetTime);
		lua_register(L, "GetDeltaTime", LuaGetDeltaTime);
		lua_register(L, "Log", LuaLog);
		lua_register(L, "SpawnPrefab", LuaSpawnPrefab);
		lua_register(L, "PlaySound", LuaPlaySound);
		lua_register(L, "StopSound", LuaStopSound);
		lua_register(L, "SetBusVolume", LuaSetBusVolume);
		lua_register(L, "GetBusVolume", LuaGetBusVolume);

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
			{ "PlaySound", LuaEntity_PlaySound },
			{ "StopSound", LuaEntity_StopSound },
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
		s_EntityScriptPaths.clear();

		// The source cache exists so many entities spawning from one file
		// read it once per session. Held across sessions it answers a new
		// Play with the previous session's file - editing a script between
		// play/stop/play would have no effect on the next run. A new session
		// reads the files as they are on disk now.
		s_ScriptSourceCache.clear();

		// Delta times are per-frame, per-session state. Left over from a
		// previous run they would make a script's first OnCreate see the last
		// session's dt - and an entity whose handle happens to be reused would
		// inherit another entity's value. The editor's play/stop/play cycle hits
		// this every time.
		s_EntityDeltaTimes.clear();
		s_LastDeltaTime = 0.0;

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

			std::string filepath = ResolveScriptPath(sc.ClassName);
			std::string error;
			if (!LoadScriptForEntity(entity, filepath, &error))
			{
				FG_CORE_ERROR("Failed to load script for entity '{0}': {1}", entity.GetName(), error);
				continue;
			}

			CallEntityFunction(entity, "OnCreate");
		}

		// Scripted entities receive physics contacts through this bridge, so
		// authors get OnCollision/OnCollisionEnd without per-entity setup.
		BindSceneCollisionHandlers(scene);
	}

	void ScriptEngine::BindSceneCollisionHandlers(Scene* scene)
	{
		if (!scene)
			return;

		auto view = scene->GetAllEntitiesWith<ScriptComponent>();
		for (auto e : view)
		{
			Entity entity{ e, scene };
			scene->SetCollisionHandler(entity, &ScriptEngine::OnEntityCollision);
		}
	}

	std::string ScriptEngine::ResolveScriptPath(const std::string& className)
	{
		std::string directory = s_ScriptDirectory;
		if (!directory.empty() && directory.back() != '/' && directory.back() != '\\')
			directory += '/';
		return directory + className + ".lua";
	}

	bool ScriptEngine::HotReloadScript(const std::string& filepath)
	{
		// Hot reload rebuilds live instances; outside a runtime session
		// there are none, and reporting success for nothing reloaded would
		// be the report-success-by-default failure this codebase has been
		// bitten by.
		if (!s_LuaState || !s_ActiveScene)
		{
			FG_CORE_WARN("Hot reload of '{0}' requested with no active runtime session", filepath);
			return false;
		}

		std::vector<uint32_t> bound;
		for (const auto& [id, path] : s_EntityScriptPaths)
		{
			if (path == filepath)
				bound.push_back(id);
		}

		// The cache must not answer with the previous source: drop it
		// before reading, so the reload verifies the file on disk rather
		// than assuming it matches what is already in memory.
		s_ScriptSourceCache.erase(filepath);

		std::string source;
		if (!FileSystem::ReadTextFile(filepath, source))
		{
			// The live instances stay running: losing a working script to a
			// deleted file is not an upgrade.
			FG_CORE_WARN("Hot reload of '{0}' failed: file not readable - previous scripts keep running", filepath);
			return false;
		}

		s_ScriptSourceCache[filepath] = source;

		if (bound.empty())
			return true;  // Nothing bound to this file; nothing to rebuild.

		size_t rebuilt = 0;
		bool allOk = true;
		for (uint32_t id : bound)
		{
			Entity entity{ static_cast<entt::entity>(id), s_ActiveScene };
			if (!entity)
			{
				// The entity died without its script being released; the
				// map entry goes with it.
				s_EntityScriptPaths.erase(id);
				continue;
			}

			std::string error;
			if (!LoadScriptForEntity(entity, filepath, &error))
			{
				// LoadScriptForEntity only swaps the instance after every
				// check passes, so this entity keeps running the script
				// that worked.
				FG_CORE_ERROR("Hot reload of '{0}' failed for entity '{1}': {2} - previous script keeps running",
					filepath, entity.GetName(), error);
				allOk = false;
				continue;
			}

			CallEntityFunction(entity, "OnCreate");
			++rebuilt;
		}

		FG_CORE_INFO("Hot reloaded '{0}': {1} instance(s) rebuilt", filepath, rebuilt);
		return allOk;
	}

	size_t ScriptEngine::HotReloadAll()
	{
		std::set<std::string> paths;
		for (const auto& [id, path] : s_EntityScriptPaths)
		{
			(void)id;
			paths.insert(path);
		}

		size_t reloaded = 0;
		for (const auto& path : paths)
		{
			if (HotReloadScript(path))
				++reloaded;
		}
		return reloaded;
	}

	void ScriptEngine::OnRuntimeStop()
	{
		if (!s_LuaState)
			return;

		// Detach the collision bridge before the script tables die, so a physics
		// step that outlives the Lua state cannot dispatch into freed state.
		if (s_ActiveScene)
		{
			for (auto& [id, ref] : s_EntityScripts)
			{
				(void)ref;
				Entity entity{ static_cast<entt::entity>(id), s_ActiveScene };
				if (entity)
					s_ActiveScene->SetCollisionHandler(entity, nullptr);
			}
		}

		// Tear down every script instance.
		for (auto& [id, ref] : s_EntityScripts)
		{
			(void)id;
			luaL_unref(s_LuaState, LUA_REGISTRYINDEX, ref);
		}
		s_EntityScripts.clear();
		s_EntityScriptPaths.clear();
		s_ActiveScene = nullptr;

		lua_pushnil(s_LuaState);
		lua_setglobal(s_LuaState, "Scene");

		FG_CORE_INFO("ScriptEngine runtime stop");
	}

	void ScriptEngine::OnCreateEntity(Entity entity)
	{
		CallEntityFunction(entity, "OnCreate");

		// Entities spawned mid-simulation must also receive contacts.
		if (s_ActiveScene && entity)
			s_ActiveScene->SetCollisionHandler(entity, &ScriptEngine::OnEntityCollision);
	}

	void ScriptEngine::OnUpdateEntity(Entity entity, Timestep ts)
	{
		// Recorded before the call, so GetDeltaTime() is correct inside OnUpdate
		// itself rather than reporting the previous frame.
		s_EntityDeltaTimes[(uint32_t)(entt::entity)entity] = ts.GetSeconds();
		s_LastDeltaTime = ts.GetSeconds();

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
			if (!FileSystem::ReadTextFile(filepath, source))
			{
				if (errorOut)
					*errorOut = "could not open " + filepath;
				return false;
			}

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
		s_EntityScriptPaths[(uint32_t)(entt::entity)entity] = filepath;
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

	void ScriptEngine::OnEntityCollision(Entity self, Entity other, const glm::vec3& normal, Physics::ContactPhase phase)
	{
		if (!s_LuaState || !self)
			return;

		const char* functionName = phase == Physics::ContactPhase::Begin ? "OnCollision" : "OnCollisionEnd";

		auto it = s_EntityScripts.find((uint32_t)(entt::entity)self);
		if (it == s_EntityScripts.end())
			return;

		lua_State* L = s_LuaState;

		lua_rawgeti(L, LUA_REGISTRYINDEX, it->second); // script table
		if (!lua_istable(L, -1))
		{
			lua_pop(L, 1);
			return;
		}

		lua_getfield(L, -1, functionName);
		if (!lua_isfunction(L, -1))
		{
			// Scripts are not required to handle collisions.
			lua_pop(L, 2);
			return;
		}

		PushEntity(L, self);
		if (other)
			PushEntity(L, other);
		else
			lua_pushnil(L);

		lua_pushnumber(L, normal.x);
		lua_pushnumber(L, normal.y);
		lua_pushnumber(L, normal.z);

		if (lua_pcall(L, 5, 0, 0) != 0)
		{
			FG_CORE_ERROR("Lua error in {0}: {1}", functionName, lua_tostring(L, -1));
			lua_pop(L, 1);
		}

		lua_pop(L, 1); // script table
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

	bool ScriptEngine::PushEntityScriptTable(Entity entity)
	{
		if (!s_LuaState || !entity)
			return false;

		auto it = s_EntityScripts.find((uint32_t)(entt::entity)entity);
		if (it == s_EntityScripts.end())
			return false;

		lua_rawgeti(s_LuaState, LUA_REGISTRYINDEX, it->second);
		if (!lua_istable(s_LuaState, -1))
		{
			lua_pop(s_LuaState, 1);
			return false;
		}

		return true;
	}

	bool ScriptEngine::GetScriptNumber(Entity entity, const char* field, double& out)
	{
		if (!field || !PushEntityScriptTable(entity))
			return false;

		lua_getfield(s_LuaState, -1, field);
		const bool ok = lua_isnumber(s_LuaState, -1);
		if (ok)
			out = lua_tonumber(s_LuaState, -1);

		lua_pop(s_LuaState, 2); // value, table
		return ok;
	}

	bool ScriptEngine::GetScriptString(Entity entity, const char* field, std::string& out)
	{
		if (!field || !PushEntityScriptTable(entity))
			return false;

		lua_getfield(s_LuaState, -1, field);
		const bool ok = lua_isstring(s_LuaState, -1);
		if (ok)
			out = lua_tostring(s_LuaState, -1);

		lua_pop(s_LuaState, 2); // value, table
		return ok;
	}

	bool ScriptEngine::SetScriptNumber(Entity entity, const char* field, double value)
	{
		if (!field || !PushEntityScriptTable(entity))
			return false;

		lua_pushnumber(s_LuaState, value);
		lua_setfield(s_LuaState, -2, field);
		lua_pop(s_LuaState, 1); // table
		return true;
	}

	bool ScriptEngine::GetScriptBool(Entity entity, const char* field, bool& out)
	{
		if (!field || !PushEntityScriptTable(entity))
			return false;

		lua_getfield(s_LuaState, -1, field);

		// Restricted to booleans on purpose. lua_toboolean treats nil and 0 as
		// false, so accepting anything would make a missing field
		// indistinguishable from a real false - exactly the "typo quietly did
		// nothing" failure this inspection API exists to rule out.
		const bool ok = lua_isboolean(s_LuaState, -1) != 0;
		if (ok)
			out = lua_toboolean(s_LuaState, -1) != 0;

		lua_pop(s_LuaState, 2); // value, table
		return ok;
	}

	bool ScriptEngine::SetScriptBool(Entity entity, const char* field, bool value)
	{
		if (!field || !PushEntityScriptTable(entity))
			return false;

		lua_pushboolean(s_LuaState, value ? 1 : 0);
		lua_setfield(s_LuaState, -2, field);
		lua_pop(s_LuaState, 1); // table
		return true;
	}

	bool ScriptEngine::SetScriptString(Entity entity, const char* field, const std::string& value)
	{
		if (!field || !PushEntityScriptTable(entity))
			return false;

		lua_pushlstring(s_LuaState, value.c_str(), value.size());
		lua_setfield(s_LuaState, -2, field);
		lua_pop(s_LuaState, 1); // table
		return true;
	}

	void ScriptEngine::ReleaseEntityScript(uint32_t entityID)
	{
		auto it = s_EntityScripts.find(entityID);
		if (it == s_EntityScripts.end())
			return;

		if (s_LuaState)
			luaL_unref(s_LuaState, LUA_REGISTRYINDEX, it->second);
		s_EntityScripts.erase(it);

		// Erased alongside the script, or the per-entity delta outlives the
		// entity it belonged to and is never reclaimed for the session.
		s_EntityDeltaTimes.erase(entityID);
		s_EntityScriptPaths.erase(entityID);
	}

	double ScriptEngine::GetEntityDeltaTime(Entity entity)
	{
		if (!entity)
			return 0.0;

		auto it = s_EntityDeltaTimes.find((uint32_t)(entt::entity)entity);
		if (it == s_EntityDeltaTimes.end())
			return 0.0;

		return it->second;
	}

	double ScriptEngine::GetLastDeltaTime()
	{
		return s_LastDeltaTime;
	}

}
