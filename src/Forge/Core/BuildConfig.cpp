#include "fgpch.h"
#include "Forge/Core/BuildConfig.h"

namespace Forge {

	const char* GetBuildConfigurationName()
	{
	#if defined(FG_DEBUG)
		return "Debug";
	#elif defined(FG_RELEASE)
		return "Release";
	#elif defined(FG_DIST)
		return "Dist";
	#else
		// Reaching here means the build system defined no configuration macro,
		// which would silently disable assertions. Worth naming explicitly.
		return "Unknown";
	#endif
	}

	bool AreAssertsEnabled()
	{
	#if defined(FG_ENABLE_ASSERTS)
		return true;
	#else
		return false;
	#endif
	}

	bool DoesAssertTrap()
	{
	#if defined(FG_DEBUG)
		return true;
	#else
		return false;
	#endif
	}

	bool IsDistributionBuild()
	{
	#if defined(FG_DIST)
		return true;
	#else
		return false;
	#endif
	}

}