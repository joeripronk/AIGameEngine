#include <catch2/catch_test_macros.hpp>

#include "Platform/Vulkan/ImGuiLayer.h"

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
