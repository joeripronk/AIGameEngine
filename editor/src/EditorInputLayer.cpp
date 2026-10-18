#include "fgpch.h"
#include "EditorInputLayer.h"

#include "editor/src/EditorApp.h"
#include "Forge/Events/Event.h"
#include "Forge/Events/KeyEvent.h"
#include "Forge/Core/Input.h"
#include "Forge/Core/KeyCodes.h"

#include "imgui.h"

namespace Forge {

	void EditorInputLayer::OnEvent(Event& event)
	{
		EventDispatcher dispatcher(event);
		dispatcher.Dispatch<KeyPressedEvent>(FG_BIND_EVENT_FN(EditorInputLayer::OnKeyPressed));
		dispatcher.Dispatch<WindowDropEvent>(FG_BIND_EVENT_FN(EditorInputLayer::OnWindowDrop));
	}

	// Dropping a file on the window opens it (the public editor layer's
	// OnWindowDrop): a scene by its extension, a project likewise. Any
	// other file is reported, not ignored - a dropped model is a request
	// nobody can see answered.
	bool EditorInputLayer::OnWindowDrop(WindowDropEvent& e)
	{
		const std::filesystem::path path{e.GetFilePath()};
		const std::string name = path.filename().string();
		const std::string extension = path.extension().string();

		EditorContext& editor = m_App.GetEditor();

		if (extension == ".forge")
		{
			const bool opened = editor.OpenScene(e.GetFilePath());
			m_App.Notify(opened ? "Opened " + name : "Failed to open " + name, !opened);
			return true;
		}

		if (extension == ".forgeproject")
		{
			const bool opened = editor.OpenProject(e.GetFilePath());
			m_App.Notify(opened ? "Opened project " + name : "Failed to open project " + name, !opened);
			return true;
		}

		m_App.Notify("Dropped file is not a scene or project: " + name, true);
		return true;
	}

	bool EditorInputLayer::OnKeyPressed(KeyPressedEvent& e)
	{
		// Held-down autorepeat must not fire the shortcuts; Hazel's editor
		// applies the same rule, and it is what stops one held Ctrl+S from
		// saving in a burst.
		if (e.GetRepeatCount() > 0)
			return false;

		const bool control = Input::IsKeyPressed(Key::LeftControl) || Input::IsKeyPressed(Key::RightControl);
		const bool shift = Input::IsKeyPressed(Key::LeftShift) || Input::IsKeyPressed(Key::RightShift);
		(void)shift;

		// Typing in a field is not a shortcut: an active ImGui widget owns the
		// keyboard, and a "D" in a name field must not duplicate the entity.
		const bool typing = ImGui::IsAnyItemActive();

		EditorContext& editor = m_App.GetEditor();

		switch (e.GetKeyCode())
		{
			case Key::N:
			{
				if (control)
				{
					m_App.CreateDefaultScene();
					m_App.Notify("New scene", false);
				}
				break;
			}
			case Key::S:
			{
				if (control)
					m_App.SaveCurrentScene();
				break;
			}
			case Key::D:
			{
				if (control && !typing && editor.HasSelection())
				{
					const Entity copy = editor.DuplicateEntity(editor.GetSelectedEntity());
					if (copy)
					{
						editor.SelectEntity(copy);
						m_App.Notify("Duplicated entity", false);
					}
					else
					{
						m_App.Notify("Failed to duplicate entity", true);
					}
				}
				break;
			}
			case Key::Delete:
			{
				if (!typing && editor.HasSelection())
				{
					editor.DestroyEntity(editor.GetSelectedEntity());
					m_App.Notify("Destroyed selected entity", false);
				}
				break;
			}
			case Key::W:
			{
				if (!typing)
					editor.SetGizmoMode(Gizmo::Mode::Translate);
				break;
			}
			case Key::E:
			{
				if (!typing)
					editor.SetGizmoMode(Gizmo::Mode::Rotate);
				break;
			}
			case Key::R:
			{
				if (!typing)
					editor.SetGizmoMode(Gizmo::Mode::Scale);
				break;
			}
			case Key::F5:
			{
				// The toolbar Play button's key: the simulation is the thing
				// an author toggles most, and the editor's play state is a
				// single boolean, so the toggle is unambiguous.
				editor.SetPlaying(!editor.IsPlaying());
				break;
			}
			default:
				break;
		}

		return false;
	}

}
