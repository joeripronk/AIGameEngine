#pragma once

#include "TetrisInputLayer.h"
#include "TetrisOverlay.h"

namespace Tetris {

// A combined input + overlay layer that handles input and displays controls.
//
// This sits on the layer stack so the engine calls both OnEvent and
// OnImGuiRender naturally - no special hooks required.
class InputOverlayLayer : public Forge::Layer
{
public:
	InputOverlayLayer(TetrisManager& manager)
		: Forge::Layer("TetrisInputOverlay"),
		  m_InputLayer(manager),
		  m_OverlayLayer()
	{
		m_InputLayer.SetOverlay(&m_OverlayLayer);
		m_OverlayLayer.SetManager(&manager);
	}

	~InputOverlayLayer() override = default;

	void OnAttach() override
	{
		m_InputLayer.OnAttach();
		m_OverlayLayer.OnAttach();
	}

	void OnDetach() override
	{
		m_InputLayer.OnDetach();
		m_OverlayLayer.OnDetach();
	}

	void OnEvent(Forge::Event& event) override
	{
		m_InputLayer.OnEvent(event);
	}

	void OnUpdate(Forge::Timestep ts) override
	{
		m_OverlayLayer.OnUpdate(ts);
	}

	void OnImGuiRender() override
	{
		m_OverlayLayer.OnImGuiRender();
	}

	// Forward the app's view-toggle callback to the input layer.
	void SetViewToggledCallback(std::function<void()> callback)
	{
		m_InputLayer.SetViewToggledCallback(std::move(callback));
	}

	// Forward the app's music-mute callback to the input layer.
	void SetMusicToggledCallback(std::function<void()> callback)
	{
		m_InputLayer.SetMusicToggledCallback(std::move(callback));
	}

	// Forward the app's music-skip callback to the input layer.
	void SetMusicSkippedCallback(std::function<void(int)> callback)
	{
		m_InputLayer.SetMusicSkippedCallback(std::move(callback));
	}

	// Let the HUD show which song the music is on.
	void SetMusic(const TetrisMusic* music)
	{
		m_OverlayLayer.SetMusic(music);
	}

private:
	InputLayer m_InputLayer;
	OverlayLayer m_OverlayLayer;
};

}  // namespace Tetris
