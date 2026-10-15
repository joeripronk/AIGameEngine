#pragma once

// What the engine was compiled as, and what that implies for assertions.
//
// This exists so the build configuration is a *queryable fact* rather than an
// assumption baked into a macro somewhere. The mapping once defined FG_DEBUG
// unconditionally, which meant every build - including the shipping one -
// trapped with SIGTRAP on an assertion failure. A test can now assert that a
// given configuration really does what it claims.
//
// The CMake side is the single source of truth; this only reflects it.

#include "Forge/Core/Base.h"

namespace Forge {

	// The configuration name: "Debug", "Release", "Dist", or "Unknown" if the
	// build was configured with something else.
	[[nodiscard]] const char* GetBuildConfigurationName();

	// True when assertions were compiled in, i.e. this is a debug build. Not
	// merely a macro test: with asserts off the checks are genuinely removed
	// rather than evaluated and logged, so this reports behaviour.
	[[nodiscard]] bool AreAssertsEnabled();

	// True when a failed assertion halts for inspection under a debugger.
	// False in a release build, where the check does not exist at all.
	[[nodiscard]] bool DoesAssertTrap();

	// True for the configuration that ships to users: no asserts, no trap.
	[[nodiscard]] bool IsDistributionBuild();

}