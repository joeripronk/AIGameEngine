#pragma once

#include "imgui.h"

#include <utility>

namespace Forge::UI {

	// Pushes a style colour for the length of a scope and pops it on the way
	// out (the Hazel ScopedStyleColor).
	//
	// ImGui's push/pop is a stack discipline, and a hand-written pair is only
	// correct on the path that reaches the pop: one `return` between them
	// leaves the colour pushed, and ImGui asserts on the unmatched pop at the
	// end of the frame. The colour then also leaks into every later window.
	// Owning the push and the pop in one object makes the early return
	// impossible.
	//
	// **The scope must end before the window it is used in ends.** ImGui
	// records the style stack depth when a window begins, and End() asserts
	// that the stack is back where it started - so a colour still pushed
	// when End() runs is the "Missing PopStyleColor()" assert. In Debug that
	// aborts the process, and in a shipping build the colour leaks into
	// every later window. Declaring the object at the top of the function
	// that draws the window puts it on the wrong side of that line, so the
	// scope belongs in a block of its own:
	//
	//     ImGui::Begin("Name");
	//     {
	//         Forge::UI::ScopedStyleColor color(ImGuiCol_Text, tint);
	//         ImGui::TextUnformatted("text");
	//     }
	//     ImGui::End();
	//
	// The `predicate` form exists because the common case is a colour chosen by
	// a condition: the object stays in scope either way, so there is no branch
	// deciding whether to pop.
	struct ScopedStyleColor
	{
		ScopedStyleColor() = default;

		ScopedStyleColor(ImGuiCol index, ImVec4 color, bool predicate = true)
			: m_Set(predicate)
		{
			if (predicate)
				ImGui::PushStyleColor(index, color);
		}

		ScopedStyleColor(ImGuiCol index, ImU32 color, bool predicate = true)
			: m_Set(predicate)
		{
			if (predicate)
				ImGui::PushStyleColor(index, color);
		}

		ScopedStyleColor(const ScopedStyleColor&) = delete;
		ScopedStyleColor& operator=(const ScopedStyleColor&) = delete;
		ScopedStyleColor(ScopedStyleColor&& other) noexcept
			: m_Set(std::exchange(other.m_Set, false))
		{
		}
		ScopedStyleColor& operator=(ScopedStyleColor&&) = delete;

		~ScopedStyleColor()
		{
			if (m_Set)
				ImGui::PopStyleColor();
		}

	private:
		bool m_Set = false;
	};

}  // namespace Forge::UI