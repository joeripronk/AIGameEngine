#pragma once

#include "Forge/Core/Timestep.h"

namespace Forge {

	class Entity;

	class ScriptableEntity
	{
	public:
		virtual ~ScriptableEntity() = default;

		virtual void OnCreate() {}
		virtual void OnUpdate(Timestep ts) {}
		virtual void OnDestroy() {}

		Entity m_Entity;
	};

}
