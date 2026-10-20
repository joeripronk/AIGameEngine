#pragma once

#include "Forge/Core/Window.h"

struct android_app;
struct AInputEvent;

namespace Forge {

	namespace Android {

		// The app the system handed over.
		//
		// The window is created by the engine, which knows nothing about
		// phones; the entry point is what knows. So the entry point leaves
		// the app here, and the window picks it up.
		void SetApp(android_app* app);
		android_app* GetApp();

	}  // namespace Android

	// The Android window: the system's own surface, and the input that
	// comes with it.
	//
	// Android has no window to create. The activity's surface *is* the
	// window, the system decides how big it is, and the input arrives as the
	// system's own events. So this window creates nothing: it takes the app
	// the system handed over and reports what the system says.
	class AndroidWindow : public Window
	{
	public:
		explicit AndroidWindow(android_app* app);
		~AndroidWindow() override;

		// Reads the system's events: the commands (the surface arriving,
		// the focus changing) and the input.
		//
		// The engine's frame loop calls this every frame, so this is where
		// the phone's events are read - the same place a desktop window's
		// events are read.
		void OnUpdate() override;

		uint32_t GetWidth() const override { return m_Data.Width; }
		uint32_t GetHeight() const override { return m_Data.Height; }

		// The surface is the framebuffer: there is no other buffer on a
		// phone, so the window's size in pixels is the surface's size.
		uint32_t GetFramebufferWidth() const override { return m_Data.FramebufferWidth; }
		uint32_t GetFramebufferHeight() const override { return m_Data.FramebufferHeight; }

		// The system owns the surface's size: it says when the size
		// changes, and there is nothing to ask for.
		void SetSize(uint32_t width, uint32_t height) override;

		void SetEventCallback(const EventCallbackFn& callback) override { m_Data.EventCallback = callback; }
		void SetVSync(bool enabled) override { m_Data.VSync = enabled; }
		bool IsVSync() const override { return m_Data.VSync; }

		// The system's window: the Vulkan surface is made from it.
		void* GetNativeWindow() const override;

		bool IsValid() const override;

		// Hands an event to the engine.
		void Dispatch(Event& event);

	private:
		// The system's commands: the surface arriving, changing, going.
		static void OnAppCmd(android_app* app, int32_t cmd);
		static int32_t OnInputEvent(android_app* app, AInputEvent* event);

		// The size the system says the surface is.
		void UpdateSize();

		android_app* m_App = nullptr;

		struct WindowData
		{
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t FramebufferWidth = 0;
			uint32_t FramebufferHeight = 0;
			bool VSync = true;
			EventCallbackFn EventCallback;
		};

		WindowData m_Data;
	};

}  // namespace Forge
