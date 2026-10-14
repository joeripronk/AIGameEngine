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
	delete app;

	return 0;
}