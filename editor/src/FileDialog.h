#pragma once

#include <string>

namespace Forge {

	// Thin wrappers over nativefiledialog-extended for the editor's Open/Save
	// menu items. The library is platform-native (Win32, Cocoa,
	// xdg-desktop-portal) - the engine's Vulkan/GLFW stack stays free of any
	// GUI toolkit, which is the whole reason for the portal backend choice.
	//
	// nullopt means "the user cancelled" - not a failure. A failure logs and
	// also returns nullopt, because a dialog that cannot open is answered the
	// same way as one the user closed: nothing was picked.
	namespace FileDialog {

		std::string OpenFile(const std::string& description, const std::string& extension);

		std::string SaveFile(const std::string& description, const std::string& extension,
			const std::string& defaultName);

	}

}
