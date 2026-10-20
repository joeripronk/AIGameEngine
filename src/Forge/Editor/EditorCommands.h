#pragma once

#include "Forge/Core/UUID.h"
#include "Forge/Editor/UndoStack.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>

namespace Forge {

	class Scene;
	class Entity;

	// The editor's commands.
	//
	// Two kinds, and two kinds are enough:
	//
	// - The transform, which is what a gizmo drag changes. It is cheap, and
	//   a drag reports a new one every frame - so it merges with the one
	//   before it and the author sees a single change.
	// - The state of an entity, which is everything else: a property edited
	//   in the inspector, a component added or removed, an entity created or
	//   destroyed. The state is the entity's serialized form, so one command
	//   covers every component there is and a component that is added to
	//   the engine later needs no new command.
	class TransformCommand : public EditorCommand
	{
	public:
		// Where the entity was, and where it is now. The "before" is the
		// caller's to say: the gizmo remembers where the entity was when
		// the drag started, and the scene only knows where it is.
		TransformCommand(Scene* scene, UUID entity, const glm::vec3& beforeTranslation,
			const glm::vec3& beforeRotation, const glm::vec3& beforeScale);

		void Undo() override;
		void Redo() override;

		// A drag is one change: the command keeps the state it started
		// from and takes the new one.
		bool MergeWith(const EditorCommand& next) override;

		[[nodiscard]] std::string Describe() const override { return "Move"; }

	private:
		Scene* m_Scene = nullptr;
		UUID m_Entity;
		glm::vec3 m_BeforeTranslation, m_BeforeRotation, m_BeforeScale;
		glm::vec3 m_AfterTranslation, m_AfterRotation, m_AfterScale;
	};

	// The state of an entity, before and after a change.
	//
	// An empty state means the entity did not exist: the undo of a created
	// entity destroys it, and the undo of a destroyed entity brings it back
	// - with the identity it had, so the scripts that name it by UUID keep
	// naming it.
	//
	// The state is the entity's serialized form, so one command covers every
	// component there is: a component that is added to the engine tomorrow
	// needs no new command.
	class EntityStateCommand : public EditorCommand
	{
	public:
		// Takes a component off an entity without recording a change of its
		// own: the command is the change, and a command that pushed another
		// command would undo itself.
		using ComponentRemover = std::function<bool(Entity, const std::string&)>;

		// The state before and the state after. An empty "before" means the
		// entity was created; an empty "after" means it was destroyed.
		EntityStateCommand(Scene* scene, UUID entity, std::string before, std::string after,
			ComponentRemover remover = nullptr);

		void Undo() override;
		void Redo() override;

		[[nodiscard]] std::string Describe() const override { return m_Description; }

		// What the editor's menu calls this change.
		void SetDescription(std::string description) { m_Description = std::move(description); }

	private:
		// Puts the entity into the given state: created and filled when the
		// state is there, gone when it is not.
		void Apply(const std::string& state);

		Scene* m_Scene = nullptr;
		UUID m_Entity;
		std::string m_Before;
		std::string m_After;
		std::string m_Description = "Edit";
		ComponentRemover m_RemoveComponent;
	};

}  // namespace Forge
