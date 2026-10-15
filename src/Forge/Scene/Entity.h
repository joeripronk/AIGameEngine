#pragma once

#include "Forge/Core/UUID.h"
#include "Scene.h"
#include "Components.h"

#include <entt/entt.hpp>

namespace Forge {

	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle, Scene* scene);
		Entity(const Entity& other) = default;

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args)
		{
			FG_CORE_ASSERT_MSG(!HasComponent<T>(), "Entity already has component!");
			T& component = m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
			m_Scene->OnComponentAdded(*this, component);
			return component;
		}

		template<typename T, typename... Args>
		T& AddOrReplaceComponent(Args&&... args)
		{
			T& component = m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
			m_Scene->OnComponentAdded<T>(*this, component);
			return component;
		}

		template<typename T>
		T& GetComponent()
		{
			FG_CORE_ASSERT_MSG(HasComponent<T>(), "Entity does not have component!");
			return m_Scene->m_Registry.get<T>(m_EntityHandle);
		}

		template<typename T>
		bool HasComponent()
		{
			return m_Scene->m_Registry.view<T>().contains(m_EntityHandle);
		}

		template<typename T>
		void RemoveComponent()
		{
			FG_CORE_ASSERT_MSG(HasComponent<T>(), "Entity does not have component!");

			// Hooked before the erase, so the Scene can still see the component
			// and release anything it owns - a physics body, most importantly.
			// Erasing first would leave the removal handler with nothing to act on.
			m_Scene->OnComponentRemoved(*this, m_Scene->m_Registry.get<T>(m_EntityHandle));

			m_Scene->m_Registry.remove<T>(m_EntityHandle);
		}

		operator bool() const { return m_EntityHandle != entt::null; }
		operator entt::entity() const { return m_EntityHandle; }
		operator uint32_t() const { return (uint32_t)m_EntityHandle; }

		UUID GetUUID() { return GetComponent<IDComponent>().ID; }
		const std::string& GetName() { return GetComponent<TagComponent>().Tag; }

		bool operator==(const Entity& other) const
		{
			return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene;
		}

		bool operator!=(const Entity& other) const
		{
			return !(*this == other);
		}
	private:
		entt::entity m_EntityHandle{ entt::null };
		Scene* m_Scene = nullptr;

		friend class Scene;
	};

	// Defined here rather than in Scene.cpp because RemoveComponent<T> is
	// instantiated from every translation unit that touches a component. Left in
	// the .cpp, the primary template had no visible definition and every such use
	// was an undefined reference.
	//
	// It lives in Entity.h because a definition needs a complete Entity, and
	// Entity is only forward-declared in Scene.h.
	//
	// Components that own engine resources declare an explicit specialisation in
	// Scene.h, which is included above and therefore seen first - so they never
	// instantiate this body.
	template<typename T>
	void Scene::OnComponentRemoved(Entity entity, T& component)
	{
		// Most components own nothing that needs releasing, so the generic case
		// does nothing. The add path static_asserts instead, because there every
		// component genuinely does need something.
		(void)entity; (void)component;
	}

	// Same reasoning as OnComponentRemoved above: the definition has to be
	// visible wherever AddComponent<T> is instantiated, and it needs a complete
	// Entity, which Scene.h only forward-declares.
	//
	// This one previously carried a static_assert to force every component to be
	// considered. With the definition in a .cpp that assert could not fire from
	// another translation unit, so a missing hook surfaced as an undefined
	// reference instead. Scene.h now declares a hook for every component, so the
	// default is genuinely only reached by a component nobody has hooked.
	template<typename T>
	void Scene::OnComponentAdded(Entity entity, T& component)
	{
		(void)entity; (void)component;
	}

}
