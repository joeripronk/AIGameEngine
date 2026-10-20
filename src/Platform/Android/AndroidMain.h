#pragma once

#include "Platform/Android/AndroidEntryPoint.h"

// The system's entry point.
//
// The system's glue calls this with the app it built, and it runs until the
// app is done.
//
// The desktop's entry point is `main`, in the engine's EntryPoint.h, and the
// game includes it. This is the phone's equivalent: the game includes it the
// same way, and the system finds the symbol in the package's library.
//
// It is a definition and not a declaration because the system's glue looks
// the symbol up by name: a static archive's member is only linked when
// something asks for it, and nothing asks for the system's entry point. So
// the definition is in the header the *game* includes, and the engine's own
// code never includes it.
extern "C" void android_main(struct android_app* app)
{
	Forge::Android::RunApplication(app);
}
