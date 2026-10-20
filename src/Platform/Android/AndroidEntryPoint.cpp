#include "fgpch.h"

#include "Platform/Android/AndroidEntryPoint.h"

#include "Platform/Android/AndroidAssets.h"
#include "Platform/Android/AndroidWindow.h"

#include "Forge/Core/Application.h"
#include "Forge/Core/EntryPoint.h"
#include "Forge/Core/Log.h"

#include <android/looper.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#include <string>

namespace {

	// The directory the assets live in, under the app's own data
	// directory: the app owns it, and it is the only place on a phone the
	// app can write to.
	constexpr const char* kAssetDirectory = "forge";

	// Reads the system's events until there are none left.
	void PumpEvents(android_app* app)
	{
		int events = 0;
		void* data = nullptr;
		int ident = ALooper_pollOnce(0, nullptr, &events, &data);
		while (ident >= 0)
		{
			if (data)
			{
				auto* source = static_cast<struct android_poll_source*>(data);
				if (source->process)
					source->process(app, source);
			}

			data = nullptr;
			ident = ALooper_pollOnce(0, nullptr, &events, &data);
		}
	}

}  // namespace

namespace Forge {
	namespace Android {

		void RunApplication(android_app* app)
		{
			Forge::Log::Init();

			if (!app || !app->activity)
			{
				FG_CORE_ERROR("Android: the system handed over no app");
				return;
			}

			// The assets first: the engine reads its files by path, and a
			// file inside a package is not a file.
			const std::string assetRoot = std::string(app->activity->internalDataPath) + "/" + kAssetDirectory;
			if (!Forge::Android::SyncAssets(app->activity->assetManager, assetRoot))
			{
				// A game with no assets is a game that draws an empty world,
				// and saying so is better than drawing it.
				FG_CORE_ERROR("Android: the game has no assets; the world will be empty");
			}

			// The window: the engine's window is the system's surface, and
			// the engine creates the window itself.
			Forge::Android::SetApp(app);

			// The surface: the system gives the app one when the activity is
			// ready, and the renderer needs it to make the Vulkan surface.
			// So the app waits for it, the way a desktop app waits for its
			// window.
			while (!app->window)
			{
				PumpEvents(app);

				if (app->destroyRequested)
				{
					FG_CORE_INFO("Android: the activity was destroyed before the window arrived");
					return;
				}
			}

			// The app, and its run loop. The engine's frame loop reads the
			// system's events, so the phone's input arrives while the game
			// plays.
			Forge::ApplicationCommandLineArgs args;
			auto* application = Forge::CreateApplication(args);
			if (!application)
			{
				FG_CORE_ERROR("Android: the app could not be created");
				return;
			}

			application->Run();
			const bool startupOk = application->StartupSucceeded();
			delete application;

			FG_CORE_INFO("Android: the app finished ({0})", startupOk ? "started" : "no window");
		}

	}  // namespace Android
}  // namespace Forge
