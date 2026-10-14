#pragma once

#include "Forge/Core/Timestep.h"
#include "Forge/Scene/Scene.h"

namespace Forge {

	class Entity;

	class ScriptableEntity
	{
	public:
		virtual ~ScriptableEntity() = default;

		virtual void OnCreate() {}
		virtual void OnUpdate(Timestep ts) { (void)ts; }
		virtual void OnDestroy() {}

		Entity& GetEntity() { return *m_Entity; }
		void SetEntity(Entity* entity) { m_Entity = entity; }
	private:
		Entity* m_Entity = nullptr;
	};

}
