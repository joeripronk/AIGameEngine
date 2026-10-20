#pragma once

struct android_app;

namespace Forge {
	namespace Android {

		// The app, run on a phone.
		//
		// The system's glue hands the app over and the engine's platform
		// layer runs it: the assets are written out, the window is the
		// system's surface, and the game's own factory makes the app - the
		// same factory a desktop build uses, so the game does not know it is
		// on a phone.
		void RunApplication(android_app* app);

	}  // namespace Android
}  // namespace Forge
