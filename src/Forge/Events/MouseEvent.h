#pragma once

#include "Forge/Events/Event.h"

// As in fgpch.h: no OpenGL headers on a Vulkan engine. Repeated rather than
// relied on through fgpch, because this header can be reached without it and a
// macro defined only in the precompiled header would arrive too late.
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace Forge {

	class MouseMovedEvent : public Event
	{
	public:
		MouseMovedEvent(float x, float y)
			: m_MouseX(x), m_MouseY(y) {}

		float GetX() const { return m_MouseX; }
		float GetY() const { return m_MouseY; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseMovedEvent: " << m_MouseX << ", " << m_MouseY;
			return ss.str();
		}

		FG_EVENT_CLASS_TYPE(MouseMoved)
		FG_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
	private:
		float m_MouseX, m_MouseY;
	};

	class MouseScrolledEvent : public Event
	{
	public:
		MouseScrolledEvent(float xOffset, float yOffset)
			: m_XOffset(xOffset), m_YOffset(yOffset) {}

		float GetXOffset() const { return m_XOffset; }
		float GetYOffset() const { return m_YOffset; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseScrolledEvent: " << m_XOffset << ", " << m_YOffset;
			return ss.str();
		}

		FG_EVENT_CLASS_TYPE(MouseScrolled)
		FG_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
	private:
		float m_XOffset, m_YOffset;
	};

	class MouseButtonEvent : public Event
	{
	public:
		int GetMouseButton() const { return m_Button; }

		FG_EVENT_CLASS_CATEGORY(EventCategoryMouseButton | EventCategoryInput)
	protected:
		MouseButtonEvent(int button)
			: m_Button(button)
		{
		}

		int m_Button;
	};

	class MouseButtonPressedEvent : public MouseButtonEvent
	{
	public:
		MouseButtonPressedEvent(int button)
			: MouseButtonEvent(button) {}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseButtonPressedEvent: " << m_Button;
			return ss.str();
		}

		FG_EVENT_CLASS_TYPE(MouseButtonPressed)
	};

	class MouseButtonReleasedEvent : public MouseButtonEvent
	{
	public:
		MouseButtonReleasedEvent(int button)
			: MouseButtonEvent(button) {}

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "MouseButtonReleasedEvent: " << m_Button;
			return ss.str();
		}

		FG_EVENT_CLASS_TYPE(MouseButtonReleased)
	};

	// MouseEvent is a base class for all mouse events.
	class MouseEvent : public Event
	{
	public:
		static EventType GetStaticType() { return EventType::MouseMoved; }
		
		float GetXOffset() const { return m_XOffset; }
		float GetYOffset() const { return m_YOffset; }
		float GetX() const { return m_X; }
		float GetY() const { return m_Y; }
		int GetButton() const { return m_Button; }

		FG_EVENT_CLASS_CATEGORY(EventCategoryMouse | EventCategoryInput)
	protected:
		MouseEvent(float x, float y)
			: m_X(x), m_Y(y), m_XOffset(0.0f), m_YOffset(0.0f), m_Button(GLFW_MOUSE_BUTTON_RIGHT) {}

		MouseEvent(float x, float y, float xOffset, float yOffset)
			: m_X(x), m_Y(y), m_XOffset(xOffset), m_YOffset(yOffset), m_Button(GLFW_MOUSE_BUTTON_RIGHT) {}

		float m_X, m_Y;
		float m_XOffset, m_YOffset;
		int m_Button;
	};

}
