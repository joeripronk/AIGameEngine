#include "fgpch.h"
#include "Forge/Editor/UndoStack.h"

namespace Forge {

	void UndoStack::Push(std::unique_ptr<EditorCommand> command)
	{
		if (!command)
			return;

		// A new command ends the future the redo stack stood for: once
		// something else has happened, the commands that were undone are not
		// the next thing to do any more.
		m_Redo.clear();

		// A change that continues the one before it is not a new entry: a
		// drag reports a change every frame, and the author made one
		// change.
		if (!m_Undo.empty() && m_Undo.back()->MergeWith(*command))
			return;

		m_Undo.push_back(std::move(command));
	}

	bool UndoStack::Undo()
	{
		if (m_Undo.empty())
			return false;

		std::unique_ptr<EditorCommand> command = std::move(m_Undo.back());
		m_Undo.pop_back();
		command->Undo();
		m_Redo.push_back(std::move(command));
		return true;
	}

	bool UndoStack::Redo()
	{
		if (m_Redo.empty())
			return false;

		std::unique_ptr<EditorCommand> command = std::move(m_Redo.back());
		m_Redo.pop_back();
		command->Redo();
		m_Undo.push_back(std::move(command));
		return true;
	}

	void UndoStack::Clear()
	{
		m_Undo.clear();
		m_Redo.clear();
	}

	std::string UndoStack::GetUndoName() const
	{
		if (m_Undo.empty())
			return {};
		return m_Undo.back()->Describe();
	}

	std::string UndoStack::GetRedoName() const
	{
		if (m_Redo.empty())
			return {};
		return m_Redo.back()->Describe();
	}

}  // namespace Forge
