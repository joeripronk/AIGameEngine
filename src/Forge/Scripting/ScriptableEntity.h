#pragma once

#include "Forge/Core/Timestep.h"
#include "Forge/Scene/Entity.h"

namespace Forge {

	class ScriptableEntity
	{
	public:
		virtual ~ScriptableEntity() = default;

		virtual void OnCreate() {}
		virtual void OnUpdate(Timestep ts) { (void)ts; }
		virtual void OnDestroy() {}

		Entity& GetEntity() { return m_Entity; }
		const Entity& GetEntity() const { return m_Entity; }

		// Takes the Entity by value and stores a copy, deliberately.
		//
		// This used to take an Entity*, which cannot work: the Scene constructs
		// its Entity wrapper on demand while iterating, so any pointer to one
		// dangles as soon as that iteration ends. The original code papered over
		// that by passing nullptr, which left a native script unable to reach its
		// own entity. A value copy has no lifetime to get wrong.
		void SetEntity(Entity entity) { m_Entity = entity; }

	private:
		Entity m_Entity;
	};

}
