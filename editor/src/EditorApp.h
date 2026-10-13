#pragma once

#include "Forge/Core/Application.h"

namespace Forge {

	class EditorApp : public Application
	{
	public:
		EditorApp();
		~EditorApp() override;

	private:
		void Run() override;
	};

}
