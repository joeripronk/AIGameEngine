#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace Forge {

	// A command that can be undone and redone.
	//
	// The editor's changes go through commands rather than through the scene
	// directly: an edit that is not a command cannot be taken back, and an
	// edit that cannot be taken back is a mistake the author has to live
	// with. That matters for an agent driving the editor as much as it does
	// for a person dragging a gizmo.
	class EditorCommand
	{
	public:
		virtual ~EditorCommand() = default;

		// Put the editor back the way it was.
		virtual void Undo() = 0;

		// Put the change back.
		virtual void Redo() = 0;

		// Two commands of the same kind can become one command: a drag that
		// reports a change every frame is one change to the author, and a
		// stack with a thousand entries in it is a stack nobody can use.
		//
		// The command asked is the *new* one; merging into it keeps the
		// older state and takes the newer one.
		virtual bool MergeWith(const EditorCommand& next)
		{
			(void)next;
			return false;
		}

		// What the editor's menu calls this change.
		[[nodiscard]] virtual std::string Describe() const { return "Edit"; }
	};

	// The stack of commands (the Godot editor's undo history).
	//
	// A new command clears the redo stack: the future the redo stack stood
	// for is no longer the future once something else has happened.
	class UndoStack
	{
	public:
		// Pushes a command. A null command is ignored - there is nothing to
		// undo, and a stack entry that does nothing is a stack entry that
		// does not.
		void Push(std::unique_ptr<EditorCommand> command);

		// Takes back the last command. False when there is nothing to take
		// back.
		[[nodiscard]] bool Undo();

		// Puts the last undone command back. False when there is nothing to
		// put back.
		[[nodiscard]] bool Redo();

		// Forgets everything. A new scene starts with an empty history: the
		// commands of the old scene would undo changes to a scene that is
		// not there.
		void Clear();

		[[nodiscard]] bool CanUndo() const { return !m_Undo.empty(); }
		[[nodiscard]] bool CanRedo() const { return !m_Redo.empty(); }

		// The names the editor's menu shows.
		[[nodiscard]] std::string GetUndoName() const;
		[[nodiscard]] std::string GetRedoName() const;

		[[nodiscard]] size_t GetUndoCount() const { return m_Undo.size(); }
		[[nodiscard]] size_t GetRedoCount() const { return m_Redo.size(); }

	private:
		std::vector<std::unique_ptr<EditorCommand>> m_Undo;
		std::vector<std::unique_ptr<EditorCommand>> m_Redo;
	};

}  // namespace Forge
