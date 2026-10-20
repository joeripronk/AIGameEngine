#pragma once

#include "Forge/Events/Event.h"

namespace Forge {

	class WindowResizeEvent : public Event
	{
	public:
		// The window's size in points and its size in pixels. The two are
		// the same number on an unscaled display and different numbers on a
		// scaled one: a 1600x900 window on a 150% display has a 2400x1350
		// framebuffer, and the framebuffer is what is presented.
		//
		// The pixel size defaults to the window size, so an event raised
		// without one still describes a window that is its own size.
		WindowResizeEvent(unsigned int width, unsigned int height,
			unsigned int framebufferWidth = 0, unsigned int framebufferHeight = 0)
			: m_Width(width), m_Height(height),
			  m_FramebufferWidth(framebufferWidth != 0 ? framebufferWidth : width),
			  m_FramebufferHeight(framebufferHeight != 0 ? framebufferHeight : height) {}

		unsigned int GetWidth() const { return m_Width; }
		unsigned int GetHeight() const { return m_Height; }

		unsigned int GetFramebufferWidth() const { return m_FramebufferWidth; }
		unsigned int GetFramebufferHeight() const { return m_FramebufferHeight; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;
			if (m_FramebufferWidth != m_Width || m_FramebufferHeight != m_Height)
				ss << " (framebuffer " << m_FramebufferWidth << ", " << m_FramebufferHeight << ")";
			return ss.str();
		}

		FG_EVENT_CLASS_TYPE(WindowResize)
		FG_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	private:
		unsigned int m_Width, m_Height;
		unsigned int m_FramebufferWidth, m_FramebufferHeight;
	};

	class WindowCloseEvent : public Event
	{
	public:
		WindowCloseEvent() = default;

		FG_EVENT_CLASS_TYPE(WindowClose)
		FG_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	// A file dropped onto the window from the desktop (the public editor
	// layer's OnWindowDrop; the event class itself is not in the public
	// core - built to the same shape as every other event here). One event
	// per dropped file: a multi-file drop is several files, not one
	// ambiguous path.
	class WindowDropEvent : public Event
	{
	public:
		WindowDropEvent(std::string filePath)
			: m_FilePath(std::move(filePath)) {}

		const std::string& GetFilePath() const { return m_FilePath; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "WindowDropEvent: " << m_FilePath;
			return ss.str();
		}

		FG_EVENT_CLASS_TYPE(WindowDrop)
		FG_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	private:
		std::string m_FilePath;
	};

	class AppTickEvent : public Event
	{
	public:
		AppTickEvent() = default;

		FG_EVENT_CLASS_TYPE(AppTick)
		FG_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class AppUpdateEvent : public Event
	{
	public:
		AppUpdateEvent() = default;

		FG_EVENT_CLASS_TYPE(AppUpdate)
		FG_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};

	class AppRenderEvent : public Event
	{
	public:
		AppRenderEvent() = default;

		FG_EVENT_CLASS_TYPE(AppRender)
		FG_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	};
}
