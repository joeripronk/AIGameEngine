#include "fgpch.h"
#include "Forge/Scene/Entity.h"

namespace Forge {

	Entity::Entity(entt::entity handle, Scene* scene)
		: m_EntityHandle(handle), m_Scene(scene)
	{
	}

}
