#pragma once

#include "Forge/Core/Layer.h"

#include "imgui.h"

#include <cstdint>

namespace Forge {

	// Owns the Dear ImGui context and the GLFW + Vulkan backends, so an
	// application gets on-screen UI by pushing one layer instead of
	// hand-rolling the backend lifecycle (the editor and Tetris both did,
	// duplicating ~50 identical lines each).
	//
	// Push it as an overlay, then drive each frame:
	//
	//     m_ImGuiLayer->NewFrame();
	//     for (Layer* layer : GetLayerStack())   // between NewFrame and Render
	//         layer->OnImGuiRender();
	//     m_ImGuiLayer->Render();
	//
	// When the device is missing, OnAttach logs the failure and IsReady()
	// stays false; NewFrame/Render then do nothing, so a game runs without
	// its overlay rather than dying with it.
	class ImGuiLayer : public Layer
	{
	public:
		ImGuiLayer();
		~ImGuiLayer() override;

		// Creates the context, configures it for keyboard navigation over the
		// 3D scene, and initialises the GLFW and Vulkan backends from the
		// running Application's window. Safe to call once per layer; OnDetach
		// performs the matching shutdown.
		void OnAttach() override;
		void OnDetach() override;

		// The same contract with the window injected. A null window (or a
		// missing swapchain) logs the reason, leaves IsReady() false and
		// destroys the context it created, so a game keeps running without
		// its overlay. OnAttach delegates here; tests drive failure paths
		// with it.
		bool AttachTo(void* nativeWindow);

		// Frame driving. NewFrame must precede any layer's OnImGuiRender,
		// Render must follow it; both are inert before OnAttach succeeded and
		// after OnDetach.
		void NewFrame();
		void Render();

		// True once the backends are live: the UI will draw. A predicate, so
		// callers may poll it every frame without handling it.
		bool IsReady() const { return m_Ready; }

		// The shared overlay look: a transparent window background so the
		// 3D scene stays visible behind panels. Applied to the current
		// context; pure, so it is unit-testable without a device.
		static void ApplyStyle(ImGuiStyle& style);

	private:
		bool m_HasContext = false;
		bool m_Ready = false;

		// ImGui_ImplVulkan_InitInfo kept alive for the backend's lifetime
		// (it references the colour-attachment format), allocated by OnAttach
		// only in Vulkan builds; void* keeps this header free of vulkan.h so
		// headless targets can include it.
		void* m_VulkanInitInfo = nullptr;
	};

}  // namespace Forge
