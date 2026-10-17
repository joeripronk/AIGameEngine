#include "fgpch.h"
#include "Forge/Core/LayerStack.h"

namespace Forge {

	LayerStack::~LayerStack()
	{
		for (Layer* layer : m_Layers)
		{
			// Detach before delete, in the same order Hazel does. A layer that
			// owns something (a GPU buffer, a lua reference, a physics body) must
			// be told the stack is going away; deleting it without OnDetach
			// leaves that ownership with nothing to release it.
			layer->OnDetach();
			delete layer;
		}
	}

	void LayerStack::PushLayer(Layer* layer)
	{
		m_Layers.insert(m_Layers.begin() + m_LayerInsertIndex, layer);
		m_LayerInsertIndex++;
	}

	void LayerStack::PushOverlay(Layer* overlay)
	{
		m_Layers.push_back(overlay);
	}

	void LayerStack::PopLayer(Layer* layer)
	{
		// Only the layer region [begin, begin + m_LayerInsertIndex) is searched.
		// Searching the whole stack let PopLayer remove an overlay, which also
		// corrupted m_LayerInsertIndex: overlays are not counted there, so the
		// index drifted and the next PushLayer inserted in the wrong place.
		auto it = std::find(m_Layers.begin(), m_Layers.begin() + m_LayerInsertIndex, layer);
		if (it == m_Layers.begin() + m_LayerInsertIndex)
			return;

		layer->OnDetach();
		m_Layers.erase(it);
		m_LayerInsertIndex--;
	}

	void LayerStack::PopOverlay(Layer* overlay)
	{
		// The overlay region is [begin + m_LayerInsertIndex, end), searched
		// front to back so the oldest matching overlay goes, not the newest -
		// a reverse search popped the last one pushed.
		auto it = std::find(m_Layers.begin() + m_LayerInsertIndex, m_Layers.end(), overlay);
		if (it == m_Layers.end())
			return;

		overlay->OnDetach();
		m_Layers.erase(it);
	}

}