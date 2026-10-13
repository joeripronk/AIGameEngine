#pragma once

#include "Forge/Core/Application.h"

namespace Forge {

	class RuntimeApp : public Application
	{
	public:
		RuntimeApp();
		~RuntimeApp() override;

	private:
		void Run() override;
	};

}
