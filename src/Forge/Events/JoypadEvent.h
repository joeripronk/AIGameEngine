#pragma once

#include "Forge/Events/Event.h"

namespace Forge {

	// A gamepad's buttons and axes.
	//
	// The window layer reads the pads and turns what changed into events, so
	// the input map is fed the same way it is fed by the keyboard: a press
	// is an event, and the map's answers are exact.
	class JoypadButtonEvent : public Event
	{
	public:
		JoypadButtonEvent(int joystick, int button, bool pressed)
			: m_Joystick(joystick), m_Button(button), m_Pressed(pressed) {}

		int GetJoystick() const { return m_Joystick; }
		int GetButton() const { return m_Button; }
		bool IsPressed() const { return m_Pressed; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "JoypadButtonEvent: pad " << m_Joystick << ", button " << m_Button
			   << (m_Pressed ? " pressed" : " released");
			return ss.str();
		}

		FG_EVENT_CLASS_TYPE(JoypadButton)
		FG_EVENT_CLASS_CATEGORY(EventCategoryInput)
	private:
		int m_Joystick;
		int m_Button;
		bool m_Pressed;
	};

	// An axis moved. The value is from -1 to 1.
	class JoypadAxisEvent : public Event
	{
	public:
		JoypadAxisEvent(int joystick, int axis, float value)
			: m_Joystick(joystick), m_Axis(axis), m_Value(value) {}

		int GetJoystick() const { return m_Joystick; }
		int GetAxis() const { return m_Axis; }
		float GetValue() const { return m_Value; }

		std::string ToString() const override
		{
			std::stringstream ss;
			ss << "JoypadAxisEvent: pad " << m_Joystick << ", axis " << m_Axis << " = " << m_Value;
			return ss.str();
		}

		FG_EVENT_CLASS_TYPE(JoypadAxis)
		FG_EVENT_CLASS_CATEGORY(EventCategoryInput)
	private:
		int m_Joystick;
		int m_Axis;
		float m_Value;
	};

}  // namespace Forge
