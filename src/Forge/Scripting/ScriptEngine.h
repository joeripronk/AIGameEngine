#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Scene/Entity.h"

#include "Forge/Scene/Scene.h"

extern "C" {
#include <lua.h>
}

namespace Forge {

	class ScriptEngine
	{
	public:
		static void Init();
		static void Shutdown();

		static void OnRuntimeStart(Scene* scene);
		static void OnRuntimeStop();

		static void OnCreateEntity(Entity entity);
		static void OnUpdateEntity(Entity entity, Timestep ts);

		static Ref<class ScriptClass> GetEntityClass(const std::string& className);

	private:
		static lua_State* s_LuaState;
	};

	class ScriptClass
	{
	public:
		virtual ~ScriptClass() = default;

		virtual void OnCreate() = 0;
		virtual void OnUpdate(Timestep ts) = 0;
		virtual void OnDestroy() = 0;
	};

}
