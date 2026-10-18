#pragma once
#include "Forge/Core/Base.h"
#include "Forge/Core/Application.h"

// The client application (editor or runtime) provides this factory.
extern Forge::Application* Forge::CreateApplication(Forge::ApplicationCommandLineArgs args);

int main(int argc, char** argv)
{
	Forge::Log::Init();

	auto app = Forge::CreateApplication({ argc, argv });
	app->Run();
	const bool startupOk = app->StartupSucceeded();
	delete app;

	// A startup that never got a window is a failure with a reason in the
	// log - and an exit code that says so, not a silent zero.
	return startupOk ? 0 : 1;
}