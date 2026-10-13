#pragma once
#include "Forge/Core/Base.h"
#include "Forge/Core/Application.h"

#ifdef FG_PLATFORM_WINDOWS

extern Forge::Application* Forge::CreateApplication(ApplicationCommandLineArgs args);

int main(int argc, char** argv)
{
	Forge::Log::Init();

	auto app = Forge::CreateApplication({ argc, argv });
	app->Run();
	delete app;
}

#endif
