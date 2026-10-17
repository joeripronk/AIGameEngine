#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Core/Layer.h"

#include <vector>

namespace Forge {

	class LayerStack
	{
	public:
		LayerStack() = default;
		~LayerStack();

		void PushLayer(Layer* layer);
		void PushOverlay(Layer* overlay);
		void PopLayer(Layer* layer);
		void PopOverlay(Layer* overlay);

		// Hazel names the two collections, so a caller (and a test) can talk
		// about "the first layer" without repeating std::vector<Layer*>::.
		using iterator = std::vector<Layer*>::iterator;
		using reverse_iterator = std::vector<Layer*>::reverse_iterator;
		using const_iterator = std::vector<Layer*>::const_iterator;
		using const_reverse_iterator = std::vector<Layer*>::const_reverse_iterator;

		iterator begin() { return m_Layers.begin(); }
		iterator end() { return m_Layers.end(); }
		reverse_iterator rbegin() { return m_Layers.rbegin(); }
		reverse_iterator rend() { return m_Layers.rend(); }

		const_iterator begin() const { return m_Layers.begin(); }
		const_iterator end()	const { return m_Layers.end(); }
		const_reverse_iterator rbegin() const { return m_Layers.rbegin(); }
		const_reverse_iterator rend() const { return m_Layers.rend(); }
	private:
		std::vector<Layer*> m_Layers;
		unsigned int m_LayerInsertIndex = 0;
	};

}
