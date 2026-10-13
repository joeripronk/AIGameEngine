#pragma once

#include "Forge/Core/EntryPoint.h"

// Define the client CreateApplication function
#include "runtime/src/RuntimeApp.h"

namespace Forge {

	Application* CreateApplication(ApplicationCommandLineArgs args)
	{
		return new RuntimeApp();
	}

}
