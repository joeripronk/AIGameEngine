#include "fgpch.h"
#include "Forge/Editor/EditorCommands.h"

#include "Forge/Core/Log.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"
#include "Forge/Scene/SceneSerializer.h"

#include <algorithm>
#include <vector>

namespace Forge {

	namespace {

		// The transform of an entity, or a zero transform when the entity has
		// none or is not there.
		void ReadTransform(Scene* scene, UUID entity, glm::vec3& translation,
			glm::vec3& rotation, glm::vec3& scale)
		{
			translation = glm::vec3(0.0f);
			rotation = glm::vec3(0.0f);
			scale = glm::vec3(1.0f);

			if (!scene)
				return;

			Entity found = scene->GetEntityByUUID(entity);
			if (!found || !found.HasComponent<TransformComponent>())
				return;

			const TransformComponent& transform = found.GetComponent<TransformComponent>();
			translation = transform.Translation;
			rotation = transform.Rotation;
			scale = transform.Scale;
		}

		void WriteTransform(Scene* scene, UUID entity, const glm::vec3& translation,
			const glm::vec3& rotation, const glm::vec3& scale)
		{
			if (!scene)
				return;

			Entity found = scene->GetEntityByUUID(entity);
			if (!found || !found.HasComponent<TransformComponent>())
				return;

			TransformComponent& transform = found.GetComponent<TransformComponent>();
			transform.Translation = translation;
			transform.Rotation = rotation;
			transform.Scale = scale;
		}

	}  // namespace

	TransformCommand::TransformCommand(Scene* scene, UUID entity, const glm::vec3& beforeTranslation,
		const glm::vec3& beforeRotation, const glm::vec3& beforeScale)
		: m_Scene(scene), m_Entity(entity),
		  m_BeforeTranslation(beforeTranslation), m_BeforeRotation(beforeRotation), m_BeforeScale(beforeScale)
	{
		// Where the entity is now is read from the scene: the caller knows
		// where it was, and only the scene knows where it is.
		ReadTransform(scene, entity, m_AfterTranslation, m_AfterRotation, m_AfterScale);
	}

	void TransformCommand::Undo()
	{
		WriteTransform(m_Scene, m_Entity, m_BeforeTranslation, m_BeforeRotation, m_BeforeScale);
	}

	void TransformCommand::Redo()
	{
		WriteTransform(m_Scene, m_Entity, m_AfterTranslation, m_AfterRotation, m_AfterScale);
	}

	bool TransformCommand::MergeWith(const EditorCommand& next)
	{
		const auto* other = dynamic_cast<const TransformCommand*>(&next);
		if (!other || other->m_Entity != m_Entity || other->m_Scene != m_Scene)
			return false;

		// The drag continues: the state the command started from stays, and
		// the state it ends at is the newer one.
		m_AfterTranslation = other->m_AfterTranslation;
		m_AfterRotation = other->m_AfterRotation;
		m_AfterScale = other->m_AfterScale;
		return true;
	}

	EntityStateCommand::EntityStateCommand(Scene* scene, UUID entity, std::string before, std::string after,
		ComponentRemover remover)
		: m_Scene(scene), m_Entity(entity), m_Before(std::move(before)), m_After(std::move(after)),
		  m_RemoveComponent(std::move(remover))
	{
	}

	void EntityStateCommand::Apply(const std::string& state)
	{
		if (!m_Scene)
			return;

		if (state.empty())
		{
			// The entity was not there: it is gone.
			Entity found = m_Scene->GetEntityByUUID(m_Entity);
			if (found)
				m_Scene->DestroyEntity(found);
			return;
		}

		Entity existing = m_Scene->GetEntityByUUID(m_Entity);

		if (state.empty())
		{
			// The entity was not there: it is gone.
			if (existing)
				m_Scene->DestroyEntity(existing);
			return;
		}

		if (existing)
		{
			// The entity is still there, and it keeps its identity in every
			// sense - the same handle, the same place in the hierarchy, the
			// same script instance.
			//
			// The components the state does not mention are taken off, and
			// the ones it does mention are set. A component can be replaced
			// but not taken away by writing a state, so the removals are
			// done first.
			const std::vector<std::string> wanted = SceneSerializer::GetStateComponents(state);
			const std::vector<std::string> current = SceneSerializer::GetStateComponents(
				SceneSerializer::SerializeEntityToString(existing));

			if (m_RemoveComponent)
			{
				for (const std::string& name : current)
				{
					if (std::find(wanted.begin(), wanted.end(), name) == wanted.end())
						(void)m_RemoveComponent(existing, name);
				}
			}

			if (SceneSerializer::ApplyEntityState(existing, state))
				return;

			// The state could not be written onto the entity. The entity is
			// made again from the state, which is the same result with a
			// different handle.
			m_Scene->DestroyEntity(existing);
		}

		// The state carries the identity, so the entity that comes back is
		// the entity that was edited.
		(void)SceneSerializer::DeserializeEntityFromString(m_Scene, state);
	}

	void EntityStateCommand::Undo()
	{
		Apply(m_Before);
	}

	void EntityStateCommand::Redo()
	{
		Apply(m_After);
	}

}  // namespace Forge
