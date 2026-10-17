#pragma once

#include "Forge/Core/Timestep.h"

#include <string>

namespace Forge {

	class Layer
	{
	public:
		Layer(const std::string& name = "Layer")
			: m_DebugName(name) {}

		virtual ~Layer() = default;

		virtual void OnAttach() {}
		virtual void OnDetach() {}

		// Timestep, not a bare float: the frame loop has one, and handing layers
		// a float meant the unit was implicit at the one place it mattered. A
		// layer that wants milliseconds asks Timestep::GetMilliseconds().
		virtual void OnUpdate(Timestep ts) { (void)ts; }
		virtual void OnImGuiRender() {}
		virtual void OnEvent(class Event& event) { (void)event; }

		const std::string& GetName() const { return m_DebugName; }
	protected:
		std::string m_DebugName;
	};

}
