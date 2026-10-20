#include "fgpch.h"

#include "Platform/Android/AndroidWindow.h"

#include "Platform/Android/AndroidKeymap.h"

#include "Forge/Core/Input.h"
#include "Forge/Core/Log.h"
#include "Forge/Events/ApplicationEvent.h"
#include "Forge/Events/KeyEvent.h"
#include "Forge/Events/MouseEvent.h"

#include <android/input.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

namespace {

	// The window the system's callbacks belong to.
	//
	// The system's glue calls back with the app, not with the window, and
	// the app does not know the window: the engine creates the window, and
	// the engine has one window per app, as it has everywhere else.
	Forge::AndroidWindow* s_CurrentWindow = nullptr;

	// The system's own event, as the engine's events.
	//
	// The engine's key codes are GLFW's and the system's are Android's, so
	// the key is translated. A key the engine does not name is a key that
	// does nothing - the same answer a desktop gives for a key it does not
	// know.
	void SendKey(Forge::AndroidWindow& window, int androidKeyCode, bool pressed, int repeatCount)
	{
		const int key = Forge::Android::TranslateKeyCode(androidKeyCode);
		if (key == 0)
			return;

		if (pressed)
		{
			Forge::KeyPressedEvent event(key, repeatCount);
			window.Dispatch(event);
		}
		else
		{
			Forge::KeyReleasedEvent event(key);
			window.Dispatch(event);
		}
	}

}  // namespace

namespace Forge {

	namespace Android {

		namespace {
			android_app* s_App = nullptr;
		}

		void SetApp(android_app* app)
		{
			s_App = app;
		}

		android_app* GetApp()
		{
			return s_App;
		}

	}  // namespace Android

	AndroidWindow::AndroidWindow(android_app* app)
		: m_App(app)
	{
		s_CurrentWindow = this;

		// The system's callbacks: the commands and the input. The glue
		// calls them with the app, and the app is the only thing it knows.
		if (m_App)
		{
			m_App->onAppCmd = &AndroidWindow::OnAppCmd;
			m_App->onInputEvent = &AndroidWindow::OnInputEvent;
		}

		UpdateSize();

		// The platform keeps the input state and reports it: there is no
		// window here to ask, and the engine's polled queries answer from
		// what the platform reports.
		Forge::Input::SetPlatformReportsState(true);
	}

	AndroidWindow::~AndroidWindow()
	{
		if (s_CurrentWindow == this)
			s_CurrentWindow = nullptr;
	}

	void AndroidWindow::UpdateSize()
	{
		if (!m_App || !m_App->window)
			return;

		const int width = ANativeWindow_getWidth(m_App->window);
		const int height = ANativeWindow_getHeight(m_App->window);
		if (width <= 0 || height <= 0)
			return;

		m_Data.Width = static_cast<uint32_t>(width);
		m_Data.Height = static_cast<uint32_t>(height);
		m_Data.FramebufferWidth = m_Data.Width;
		m_Data.FramebufferHeight = m_Data.Height;
	}

	void AndroidWindow::Dispatch(Event& event)
	{
		if (m_Data.EventCallback)
			m_Data.EventCallback(event);
	}

	void AndroidWindow::OnUpdate()
	{
		if (!m_App || !m_App->looper)
			return;

		// The system's events. The commands come through the glue's own
		// source (the surface arriving, the focus changing) and the input
		// through the input queue's - both are read by polling the system's
		// looper, which is what the glue's process functions expect.
		int events = 0;
		void* data = nullptr;
		int ident = ALooper_pollOnce(0, nullptr, &events, &data);
		while (ident >= 0)
		{
			if (data)
			{
				auto* source = static_cast<struct android_poll_source*>(data);
				if (source->process)
					source->process(m_App, source);
			}

			data = nullptr;
			ident = ALooper_pollOnce(0, nullptr, &events, &data);
		}
	}

	void AndroidWindow::SetSize(uint32_t width, uint32_t height)
	{
		// The system owns the surface's size. Asking for a size is not a
		// thing a phone can do: the window is the system's, and it says
		// when the size changes.
		(void)width;
		(void)height;
	}

	void* AndroidWindow::GetNativeWindow() const
	{
		return m_App ? m_App->window : nullptr;
	}

	bool AndroidWindow::IsValid() const
	{
		return m_App != nullptr && m_App->window != nullptr;
	}

	void AndroidWindow::OnAppCmd(android_app* app, int32_t cmd)
	{
		if (!s_CurrentWindow)
			return;

		switch (cmd)
		{
		case APP_CMD_INIT_WINDOW:
		case APP_CMD_WINDOW_RESIZED:
		{
			// The surface arrived, or changed shape: the frame is a new
			// size, and everything drawn into it has to know.
			s_CurrentWindow->UpdateSize();
			WindowResizeEvent event(s_CurrentWindow->m_Data.Width, s_CurrentWindow->m_Data.Height,
				s_CurrentWindow->m_Data.FramebufferWidth, s_CurrentWindow->m_Data.FramebufferHeight);
			s_CurrentWindow->Dispatch(event);
			break;
		}

		case APP_CMD_TERM_WINDOW:
		{
			// The surface is gone: the app is closing.
			WindowCloseEvent event;
			s_CurrentWindow->Dispatch(event);
			break;
		}

		case APP_CMD_LOST_FOCUS:
		{
			// The keys the system is holding down are the system's now: a
			// key that was down when the app lost the focus is up again.
			// (A phone has no focus events for the pointer, but the
			// keyboard's state is the system's while the app is away.)
			break;
		}

		default:
			break;
		}

		(void)app;
	}

	int32_t AndroidWindow::OnInputEvent(android_app* app, AInputEvent* input)
	{
		(void)app;

		if (!s_CurrentWindow || !input)
			return 0;

		switch (AInputEvent_getType(input))
		{
		case AINPUT_EVENT_TYPE_KEY:
		{
			const int action = AKeyEvent_getAction(input);
			const int keyCode = AKeyEvent_getKeyCode(input);
			const int repeat = AKeyEvent_getRepeatCount(input);

			if (action == AKEY_EVENT_ACTION_DOWN)
				SendKey(*s_CurrentWindow, keyCode, true, repeat);
			else if (action == AKEY_EVENT_ACTION_UP)
				SendKey(*s_CurrentWindow, keyCode, false, 0);
			break;
		}

		case AINPUT_EVENT_TYPE_MOTION:
		{
			const int action = AMotionEvent_getAction(input);
			const float x = AMotionEvent_getX(input, 0);
			const float y = AMotionEvent_getY(input, 0);

			// A touch is the pointer: the first finger is the left button,
			// and the engine's mouse is what a finger is.
			if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN)
			{
				MouseMovedEvent moved(x, y);
				s_CurrentWindow->Dispatch(moved);
				MouseButtonPressedEvent pressed(Mouse::ButtonLeft);
				s_CurrentWindow->Dispatch(pressed);
			}
			else if (action == AMOTION_EVENT_ACTION_MOVE)
			{
				MouseMovedEvent moved(x, y);
				s_CurrentWindow->Dispatch(moved);
			}
			else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_POINTER_UP
				|| action == AMOTION_EVENT_ACTION_CANCEL)
			{
				MouseMovedEvent moved(x, y);
				s_CurrentWindow->Dispatch(moved);
				MouseButtonReleasedEvent released(Mouse::ButtonLeft);
				s_CurrentWindow->Dispatch(released);
			}
			break;
		}

		default:
			break;
		}

		return 0;
	}

}  // namespace Forge
