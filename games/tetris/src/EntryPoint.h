#pragma once
#include "Forge/Core/EntryPoint.h"

#if defined(FG_PLATFORM_ANDROID)
	// The phone's entry point: the system's glue calls `android_main`, and
	// the engine's Android layer runs the app. The desktop's `main` is
	// compiled out there, so this is the game's entry point on a phone -
	// the same place the desktop's is.
	#include "Platform/Android/AndroidMain.h"
#endif
