#include <catch2/catch_test_macros.hpp>

#include "Forge/Editor/EditorCommands.h"
#include "Forge/Editor/EditorContext.h"
#include "Forge/Editor/UndoStack.h"
#include "Forge/Scene/Components.h"
#include "Forge/Scene/Entity.h"
#include "Forge/Scene/Scene.h"

using namespace Forge;

namespace {

	// A command that counts what it was asked to do, so the stack's own
	// behaviour can be checked without a scene.
	class CountingCommand : public EditorCommand
	{
	public:
		explicit CountingCommand(std::string name) : m_Name(std::move(name)) {}

		void Undo() override { ++m_Undos; }
		void Redo() override { ++m_Redos; }

		[[nodiscard]] std::string Describe() const override { return m_Name; }

		int m_Undos = 0;
		int m_Redos = 0;

	private:
		std::string m_Name;
	};

	// A command that merges with the next one, the way a drag does.
	class MergingCommand : public EditorCommand
	{
	public:
		void Undo() override {}
		void Redo() override {}

		bool MergeWith(const EditorCommand& next) override
		{
			const auto* other = dynamic_cast<const MergingCommand*>(&next);
			if (!other)
				return false;
			m_Merged = other->m_Merged + 1;
			return true;
		}

		int m_Merged = 0;
	};

}  // namespace

TEST_CASE("The undo stack takes back the last command and puts it back", "[editor][undo]")
{
	UndoStack stack;

	REQUIRE_FALSE(stack.CanUndo());
	REQUIRE_FALSE(stack.CanRedo());
	REQUIRE_FALSE(stack.Undo());
	REQUIRE_FALSE(stack.Redo());

	auto first = std::make_unique<CountingCommand>("First");
	CountingCommand* firstPtr = first.get();
	stack.Push(std::move(first));

	auto second = std::make_unique<CountingCommand>("Second");
	CountingCommand* secondPtr = second.get();
	stack.Push(std::move(second));

	REQUIRE(stack.CanUndo());
	REQUIRE(stack.GetUndoCount() == 2);
	REQUIRE(stack.GetUndoName() == "Second");

	// Undo takes back the last command first.
	REQUIRE(stack.Undo());
	REQUIRE(secondPtr->m_Undos == 1);
	REQUIRE(firstPtr->m_Undos == 0);
	REQUIRE(stack.CanRedo());
	REQUIRE(stack.GetRedoName() == "Second");

	// Redo puts it back.
	REQUIRE(stack.Redo());
	REQUIRE(secondPtr->m_Redos == 1);
	REQUIRE(stack.GetUndoCount() == 2);
	REQUIRE_FALSE(stack.CanRedo());

	// And the one before it.
	REQUIRE(stack.Undo());
	REQUIRE(stack.Undo());
	REQUIRE(firstPtr->m_Undos == 1);
	REQUIRE_FALSE(stack.CanUndo());
}

TEST_CASE("A new command ends the future the redo stack stood for", "[editor][undo]")
{
	UndoStack stack;

	auto first = std::make_unique<CountingCommand>("First");
	stack.Push(std::move(first));
	REQUIRE(stack.Undo());
	REQUIRE(stack.CanRedo());

	// Something else happened, so the undone command is not the next thing
	// to do any more.
	stack.Push(std::make_unique<CountingCommand>("Second"));
	REQUIRE_FALSE(stack.CanRedo());
	REQUIRE(stack.GetUndoCount() == 1);
}

TEST_CASE("A command that continues the one before it is not a new entry", "[editor][undo]")
{
	UndoStack stack;

	stack.Push(std::make_unique<MergingCommand>());
	stack.Push(std::make_unique<MergingCommand>());
	stack.Push(std::make_unique<MergingCommand>());

	// Three frames of one drag are one change.
	REQUIRE(stack.GetUndoCount() == 1);
}

TEST_CASE("A null command is not a command", "[editor][undo]")
{
	UndoStack stack;
	stack.Push(nullptr);
	REQUIRE(stack.GetUndoCount() == 0);
	REQUIRE_FALSE(stack.CanUndo());
}

TEST_CASE("A created entity is taken back by the undo", "[editor][undo]")
{
	EditorContext editor;

	Entity entity = editor.CreateEntity("Cube");
	REQUIRE(entity);
	REQUIRE(editor.GetUndoCount() == 1);
	REQUIRE(editor.CanUndo());

	// The undo takes the entity away.
	REQUIRE(editor.Undo());
	REQUIRE(editor.GetAllEntities().empty());
	REQUIRE(editor.CanRedo());

	// And the redo brings it back.
	REQUIRE(editor.Redo());
	REQUIRE(editor.GetAllEntities().size() == 1);
	REQUIRE(editor.FindEntityByName("Cube"));
}

TEST_CASE("A destroyed entity comes back with its components", "[editor][undo]")
{
	EditorContext editor;

	Entity entity = editor.CreateEntity("Cube");
	entity.GetComponent<TransformComponent>().Translation = { 1.0f, 2.0f, 3.0f };
	entity.AddComponent<BoxColliderComponent>();
	entity.GetComponent<BoxColliderComponent>().Size = { 2.0f, 2.0f, 2.0f };
	const UUID uuid = entity.GetUUID();

	editor.DestroyEntity(entity);
	REQUIRE(editor.GetAllEntities().empty());

	// The undo brings it back - with the components it had and the identity
	// it had, so a script that names it by UUID still names it.
	REQUIRE(editor.Undo());
	REQUIRE(editor.GetAllEntities().size() == 1);

	Entity restored = editor.GetEntityByUUID(uuid);
	REQUIRE(restored);
	REQUIRE(restored.GetName() == "Cube");
	REQUIRE(restored.HasComponent<BoxColliderComponent>());
	REQUIRE(restored.GetComponent<TransformComponent>().Translation == glm::vec3(1.0f, 2.0f, 3.0f));
	REQUIRE(restored.GetComponent<BoxColliderComponent>().Size == glm::vec3(2.0f, 2.0f, 2.0f));
}

TEST_CASE("A component that was added is taken back, and one that was removed comes back", "[editor][undo]")
{
	EditorContext editor;

	Entity entity = editor.CreateEntity("Cube");

	// The component that was added is gone again.
	REQUIRE(editor.AddComponentByName(entity, "BoxColliderComponent"));
	REQUIRE(entity.HasComponent<BoxColliderComponent>());
	REQUIRE(editor.Undo());
	REQUIRE_FALSE(entity.HasComponent<BoxColliderComponent>());

	// And a component that was removed is back, with the values it had.
	entity.AddComponent<BoxColliderComponent>();
	entity.GetComponent<BoxColliderComponent>().Density = 3.0f;
	REQUIRE(editor.RemoveComponentByName(entity, "BoxColliderComponent"));
	REQUIRE_FALSE(entity.HasComponent<BoxColliderComponent>());

	REQUIRE(editor.Undo());
	REQUIRE(entity.HasComponent<BoxColliderComponent>());
	REQUIRE(entity.GetComponent<BoxColliderComponent>().Density == 3.0f);
}

TEST_CASE("A property that was edited in the inspector is taken back", "[editor][undo]")
{
	EditorContext editor;

	Entity entity = editor.CreateEntity("Cube");
	entity.AddComponent<BoxColliderComponent>();
	entity.GetComponent<BoxColliderComponent>().Density = 1.0f;

	// The editor wraps the edit: the state before, and the state after.
	editor.BeginEntityEdit(entity);
	entity.GetComponent<BoxColliderComponent>().Density = 5.0f;
	editor.CommitEntityEdit();

	REQUIRE(entity.GetComponent<BoxColliderComponent>().Density == 5.0f);
	REQUIRE(editor.CanUndo());

	REQUIRE(editor.Undo());
	REQUIRE(entity.GetComponent<BoxColliderComponent>().Density == 1.0f);

	// And the redo puts the new value back.
	REQUIRE(editor.Redo());
	REQUIRE(entity.GetComponent<BoxColliderComponent>().Density == 5.0f);
}

TEST_CASE("An edit that changed nothing pushes nothing", "[editor][undo]")
{
	EditorContext editor;

	Entity entity = editor.CreateEntity("Cube");
	const size_t before = editor.GetUndoCount();

	editor.BeginEntityEdit(entity);
	editor.CommitEntityEdit();

	// A command that undoes nothing is a command that fills the history
	// with nothing.
	REQUIRE(editor.GetUndoCount() == before);
}

TEST_CASE("A gizmo drag is one change", "[editor][undo]")
{
	EditorContext editor;

	Entity entity = editor.CreateEntity("Cube");
	const size_t before = editor.GetUndoCount();

	// The gizmo reports the entity's position every frame of the drag, and
	// remembers where the entity was when the drag started.
	const glm::vec3 start = entity.GetComponent<TransformComponent>().Translation;
	for (int i = 1; i <= 5; ++i)
	{
		entity.GetComponent<TransformComponent>().Translation = { static_cast<float>(i), 0.0f, 0.0f };
		editor.RecordTransformChange(entity, start, glm::vec3(0.0f), glm::vec3(1.0f));
	}

	REQUIRE(editor.GetUndoCount() == before + 1);

	// The undo puts the entity back where the drag started.
	REQUIRE(editor.Undo());
	REQUIRE(entity.GetComponent<TransformComponent>().Translation == glm::vec3(0.0f, 0.0f, 0.0f));
}

TEST_CASE("A new scene starts with an empty history", "[editor][undo]")
{
	EditorContext editor;

	editor.CreateEntity("Cube");
	REQUIRE(editor.GetUndoCount() == 1);

	// The commands of the old scene would undo changes to a scene that is
	// not there any more.
	editor.NewScene();
	REQUIRE(editor.GetUndoCount() == 0);
	REQUIRE_FALSE(editor.CanUndo());
	REQUIRE_FALSE(editor.Undo());
}
