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
