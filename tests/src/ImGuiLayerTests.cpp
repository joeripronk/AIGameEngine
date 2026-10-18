#include <catch2/catch_test_macros.hpp>

#include "Platform/Vulkan/ImGuiLayer.h"
#include "Forge/UI/UI.h"

#include "imgui.h"

#include <string>

using Forge::ImGuiLayer;

TEST_CASE("ImGuiLayer starts inert", "[renderer][imgui]")
{
	ImGuiLayer layer;

	REQUIRE(layer.GetName() == "ImGuiLayer");
	REQUIRE_FALSE(layer.IsReady());

	// Frame driving before a successful attach must be inert, not a call
	// into a context that does not exist.
	layer.NewFrame();
	layer.Render();
	layer.OnDetach();

	REQUIRE_FALSE(layer.IsReady());
}

TEST_CASE("ImGuiLayer attach without a window fails and rolls back", "[renderer][imgui]")
{
	ImGuiLayer layer;

	// The failure path creates the context, discovers there is no window,
	// destroys the context again and reports failure - it must not leave a
	// dangling half-initialised layer behind.
	REQUIRE_FALSE(layer.AttachTo(nullptr));
	REQUIRE_FALSE(layer.IsReady());

	// A second attach attempt re-runs the same contract.
	REQUIRE_FALSE(layer.AttachTo(nullptr));

	layer.NewFrame();
	layer.Render();
	layer.OnDetach();
}

TEST_CASE("ImGuiLayer overlay style keeps the scene visible", "[renderer][imgui]")
{
	ImGui::CreateContext();

	ImGuiStyle& style = ImGui::GetStyle();
	const ImVec4 opaque = style.Colors[ImGuiCol_WindowBg];
	REQUIRE(opaque.w > 0.0f);

	ImGuiLayer::ApplyStyle(style);

	// The whole point of the style call: the overlay draws on top of the
	// 3D scene, so its window background must not hide it.
	REQUIRE(style.Colors[ImGuiCol_WindowBg].w == 0.0f);

	ImGui::DestroyContext();
}

TEST_CASE("ImGuiLayer configures the context for keyboard navigation and docking", "[renderer][imgui]")
{
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	const ImGuiConfigFlags before = io.ConfigFlags;

	ImGuiLayer::ApplyConfigFlags(io);

	// Navigation over the scene without a mouse, and docking for the
	// editor's DockSpace layout. These two flags are the whole contract -
	// AttachTo applies exactly this function.
	CHECK(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard);
	CHECK(io.ConfigFlags & ImGuiConfigFlags_DockingEnable);

	// Additive: whatever the embedder configured survives.
	io.ConfigFlags = before;
	ImGuiLayer::ApplyConfigFlags(io);
	CHECK((io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) == ImGuiConfigFlags_NavEnableKeyboard);
	CHECK((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == ImGuiConfigFlags_DockingEnable);

	ImGui::DestroyContext();
}

TEST_CASE("ImGuiLayer detaches cleanly after a context-only attach", "[renderer][imgui]")
{
	ImGuiLayer layer;

	// Headless contract: detach after an attach that never became ready
	// must destroy the context it created and leave the layer reusable.
	REQUIRE_FALSE(layer.AttachTo(nullptr));
	layer.OnDetach();
	REQUIRE_FALSE(layer.IsReady());

	REQUIRE_FALSE(layer.AttachTo(nullptr));
	layer.NewFrame();
	layer.Render();
	layer.OnDetach();
}

TEST_CASE("HasSavedDockLayout reads the layout from the .ini contents", "[renderer][imgui]")
{
	// The editor builds its initial dock layout only when the .ini describes
	// one, so the predicate is the whole difference between "reopen the way
	// the author left it" and "no panels at all".

	// Nothing loaded: no layout.
	CHECK_FALSE(ImGuiLayer::HasSavedDockLayout(""));

	// A .ini written by a build that docked nothing - the window entries are
	// all there is. Honouring this is the bug: every panel comes back as a
	// floating window at its saved size, stacked where the last session left
	// it, which reads as an editor with no GUI.
	const std::string windowOnly =
		"[Window][Debug##Default]\n"
		"Pos=69,66\n"
		"Size=400,400\n"
		"Collapsed=0\n"
		"\n"
		"[Window][Viewport]\n"
		"Pos=60,60\n"
		"Size=32,35\n"
		"Collapsed=0\n"
		"\n";
	CHECK_FALSE(ImGuiLayer::HasSavedDockLayout(windowOnly));

	// A window *named* "Docking" is not a dock layout. Only a section header
	// selects a section.
	const std::string decoy =
		"[Window][Docking]\n"
		"Pos=10,10\n"
		"Size=200,200\n"
		"Collapsed=0\n"
		"\n";
	CHECK_FALSE(ImGuiLayer::HasSavedDockLayout(decoy));

	// The header alone: a docking build that never built a tree. There is no
	// arrangement to honour, so the initial layout must be built.
	CHECK_FALSE(ImGuiLayer::HasSavedDockLayout("[Docking][Data]\n"));

	// Windows that claim a dock id with no tree to place them in.
	const std::string orphanDockId =
		"[Window][Viewport]\n"
		"Pos=60,60\n"
		"Size=32,35\n"
		"Collapsed=0\n"
		"DockNode=0x12345678\n"
		"DockPos=100,100\n"
		"\n";
	CHECK_FALSE(ImGuiLayer::HasSavedDockLayout(orphanDockId));

	// A real saved tree: the dockspace node and its children.
	const std::string savedLayout =
		"[Window][Viewport]\n"
		"Pos=60,60\n"
		"Size=32,35\n"
		"Collapsed=0\n"
		"DockNode=0x8F1E1D5C\n"
		"DockPos=321,0\n"
		"\n"
		"[Docking][Data]\n"
		"DockSpace       ID=0x8F1E1D5C Window=0x1A2B3C4D Pos=0,19 Size=1600,881 Split=X\n"
		"  DockNode      ID=0x11111111 Parent=0x8F1E1D5C SizeRef=319,881 Split=Y\n"
		"    DockNode    ID=0x22222222 Parent=0x11111111 SizeRef=319,610 selected=0x33333333\n"
		"\n";
	CHECK(ImGuiLayer::HasSavedDockLayout(savedLayout));

	// Indentation, tabs, CRLF and a trailing line without a newline are all
	// shapes ImGui actually writes; none of them may hide the entries.
	CHECK(ImGuiLayer::HasSavedDockLayout("[Docking][Data]\r\n\tDockSpace ID=0x1 Pos=0,0 Size=800,600\r\n"));
	CHECK(ImGuiLayer::HasSavedDockLayout("[Docking][Data]\nDockSpace ID=0x1"));
}

TEST_CASE("ScopedStyleColor pops on every path out of a scope", "[renderer][imgui]")
{
	ImGui::CreateContext();

	const ImVec4 original = ImGui::GetStyleColorVec4(ImGuiCol_Text);
	const ImVec4 highlight(1.0f, 0.0f, 0.0f, 1.0f);

	{
		Forge::UI::ScopedStyleColor color(ImGuiCol_Text, highlight);
		REQUIRE(ImGui::GetStyleColorVec4(ImGuiCol_Text).x == 1.0f);

		// The point of the object: an early return - or any other way out of
		// the scope - still pops. ImGui asserts on an unmatched pop at the end
		// of the frame, and a leaked colour would tint every later window.
	}

	REQUIRE(ImGui::GetStyleColorVec4(ImGuiCol_Text).x == original.x);
	REQUIRE(ImGui::GetStyleColorVec4(ImGuiCol_Text).w == original.w);

	// The predicate form pushes nothing when the condition is false, and still
	// pops nothing - so a colour chosen by a branch needs no branch around it.
	{
		Forge::UI::ScopedStyleColor skipped(ImGuiCol_Text, highlight, false);
		REQUIRE(ImGui::GetStyleColorVec4(ImGuiCol_Text).x == original.x);
	}

	REQUIRE(ImGui::GetStyleColorVec4(ImGuiCol_Text).x == original.x);

	// The packed-colour overload is the same contract.
	{
		Forge::UI::ScopedStyleColor packed(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
		REQUIRE(ImGui::GetStyleColorVec4(ImGuiCol_Text).y > 0.9f);
	}
	REQUIRE(ImGui::GetStyleColorVec4(ImGuiCol_Text).y == original.y);

	ImGui::DestroyContext();
}
