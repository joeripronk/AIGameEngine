#pragma once

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
		virtual void OnUpdate(float ts) { (void)ts; }
		virtual void OnImGuiRender() {}
		virtual void OnEvent(class Event& event) { (void)event; }

		const std::string& GetName() const { return m_DebugName; }
	protected:
		std::string m_DebugName;
	};

}
