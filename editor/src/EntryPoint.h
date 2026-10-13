#pragma once

#include "Forge/Core/EntryPoint.h"

// Define the client CreateApplication function
#include "editor/src/EditorApp.h"

namespace Forge {

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new EditorApp();
	}

}
