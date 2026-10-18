#include "fgpch.h"
#include "FileDialog.h"

#include "Forge/Core/Log.h"

#include <nfd.h>

#include <utility>

namespace Forge {

	namespace {

		// nfd has a global init on some backends (portal opens a bus
		// connection). Init once for the process; there is no deinit, which
		// is correct for a static-lifetime library used for the whole run -
		// and matching the library's own guidance that skipping deinit is
		// supported.
		struct NfdGlobals
		{
			NfdGlobals()
			{
				if (NFD_Init() != NFD_OKAY)
					FG_CORE_ERROR("FileDialog: native dialog backend failed to initialise");
			}
			~NfdGlobals() { NFD_Quit(); }
		};

		NfdGlobals& Globals()
		{
			static NfdGlobals s_Globals;
			return s_Globals;
		}

		// RAII over the library's malloc'd path strings: every exit from the
		// dialog, including the cancel and error paths, frees.
		struct NfdPath
		{
			nfdu8char_t* Path = nullptr;
			~NfdPath() { if (Path) NFD_FreePathU8(Path); }
			nfdu8char_t** Slot() { return &Path; }
			explicit operator bool() const { return Path != nullptr; }
			std::string Value() const { return Path ? std::string(Path) : std::string(); }
		};

	}

	std::string FileDialog::OpenFile(const std::string& description, const std::string& extension)
	{
		Globals();

		const nfdu8filteritem_t filter{ description.c_str(), extension.c_str() };

		NfdPath path;
		const nfdresult_t result = NFD_OpenDialogU8(path.Slot(), &filter, 1, nullptr);

		if (result == NFD_CANCEL)
			return {};

		if (result != NFD_OKAY)
		{
			FG_CORE_ERROR("FileDialog: OpenFile failed: {0}", NFD_GetError());
			return {};
		}

		return path.Value();
	}

	std::string FileDialog::SaveFile(const std::string& description, const std::string& extension,
		const std::string& defaultName)
	{
		Globals();

		const nfdu8filteritem_t filter{ description.c_str(), extension.c_str() };

		NfdPath path;
		const nfdresult_t result = NFD_SaveDialogU8(path.Slot(), &filter, 1, nullptr,
			defaultName.empty() ? nullptr : defaultName.c_str());

		if (result == NFD_CANCEL)
			return {};

		if (result != NFD_OKAY)
		{
			FG_CORE_ERROR("FileDialog: SaveFile failed: {0}", NFD_GetError());
			return {};
		}

		return path.Value();
	}

}
