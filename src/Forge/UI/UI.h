#pragma once

#include "imgui.h"

#include <utility>

namespace Forge::UI {

	// Pushes a style colour for the length of a scope and pops it on the way
	// out (the Hazel ScopedStyleColor).
	//
	// ImGui's push/pop is a stack discipline, and a hand-written pair is only
	// correct on the path that reaches the pop: one `return` between them -
	// the notification panel, an early-out on a collapsed panel - leaves the
	// colour pushed, and ImGui asserts on the unmatched pop at the end of the
	// frame. The colour then also leaks into every later window. Owning the
	// push and the pop in one object makes the early return impossible.
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