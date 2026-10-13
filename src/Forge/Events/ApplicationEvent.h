#pragma once

#include "Forge/Events/Event.h"

namespace Forge {

	class WindowResizeEvent : public Event
	{
	public:
		WindowResizeEvent(unsigned int width, unsigned int height)
			: m_Width(width), m_Height(height) {}

		unsigned int GetWidth() const { return m_Width; }
		unsigned int GetHeight() const { return m_Height; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "WindowResizeEvent: " << m_Width << ", " << m_Height;
			return ss.str();
		}

		FG_EVENT_CLASS_TYPE(WindowResize)
		FG_EVENT_CLASS_CATEGORY(EventCategoryApplication)
	private:
		unsigned int m_Width, m_Height;
	};

	class WindowCloseEvent : public Event
	{
	public:
		WindowCloseEvent() = default;

		FG_EVENT_CLASS_TYPE(WindowClose)
		FG_EVENT_CLASS_CATEGORY(EventCategoryApplication)
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
