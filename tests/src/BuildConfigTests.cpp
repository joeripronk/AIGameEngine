// Build configuration.
//
// The mapping from CMake configuration to compile definitions once defined
// FG_DEBUG unconditionally, so a Release build inherited FG_ENABLE_ASSERTS and
// a SIGTRAP debugbreak. A shipped game would take SIGTRAP on an assertion
// failure: no log line, no diagnostic, just a crash.
//
// These tests pin the contract. They run identically in every configuration;
// what differs is which branch they are asserting, so the suite is meaningful
// whether CI builds Debug or Dist.

#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "Forge/Core/Base.h"
#include "Forge/Core/Log.h"
#include "Forge/Core/BuildConfig.h"

TEST_CASE("The build reports a known configuration", "[core][build]")
{
	Forge::Log::Init();

	const char* name = Forge::GetBuildConfigurationName();

	// "Unknown" would mean CMake defined no configuration macro at all, which
	// silently compiles out every assert. It is a legal state to detect but
	// never a correct one to ship.
	REQUIRE(std::strcmp(name, "Unknown") != 0);

	// The name has to be one this engine actually understands, or the
	// assert/trap expectations below would be guesses.
	const bool known = (std::strcmp(name, "Debug") == 0)
		|| (std::strcmp(name, "Release") == 0)
		|| (std::strcmp(name, "Dist") == 0);

	REQUIRE(known);
}

TEST_CASE("Asserts are enabled in debug and compiled out elsewhere", "[core][build]")
{
	Forge::Log::Init();

	const bool debug = (std::strcmp(Forge::GetBuildConfigurationName(), "Debug") == 0);

	// FG_ENABLE_ASSERTS is derived from FG_DEBUG, so these must agree.
	REQUIRE(Forge::AreAssertsEnabled() == debug);

	// The trap is what made the old mapping harmful: a Release build that traps
	// on an assert takes the process down with no diagnostic.
	REQUIRE(Forge::DoesAssertTrap() == debug);
}

TEST_CASE("Only the Dist configuration claims to ship", "[core][build]")
{
	Forge::Log::Init();

	const bool dist = (std::strcmp(Forge::GetBuildConfigurationName(), "Dist") == 0);
	REQUIRE(Forge::IsDistributionBuild() == dist);

	if (dist)
	{
		// A shipping build must not halt the game on an internal invariant.
		REQUIRE_FALSE(Forge::AreAssertsEnabled());
		REQUIRE_FALSE(Forge::DoesAssertTrap());
	}
}

TEST_CASE("At most one configuration macro is defined", "[core][build]")
{
	// Precedence in GetBuildConfigurationName is Debug > Release > Dist, so two
	// macros being defined would mean the build system disagrees with itself
	// about what it built. This catches that without needing CMake.
	//
	// #ifdef is a preprocessor directive rather than a conditional operator, so
	// this has to be spelled out rather than summed arithmetically.
	#ifdef FG_DEBUG
	constexpr int defined = 1;
	#elif defined(FG_RELEASE) || defined(FG_DIST)
	constexpr int defined = 1;
	#else
	constexpr int defined = 0;
	#endif

	// Zero is legitimate only for a build configured with something unknown,
	// which the first test already rejects; two or more is always a bug.
	REQUIRE(defined <= 1);

	#ifdef FG_DEBUG
	#ifdef FG_RELEASE
	FAIL("FG_DEBUG and FG_RELEASE are both defined");
	#endif
	#ifdef FG_DIST
	FAIL("FG_DEBUG and FG_DIST are both defined");
	#endif
	#endif

	#ifdef FG_RELEASE
	#ifdef FG_DIST
	FAIL("FG_RELEASE and FG_DIST are both defined");
	#endif
	#endif
}
TEST_CASE("Log verbosity follows the configuration", "[core][build]")
{
	Forge::Log::Init();

	const bool dist = Forge::IsDistributionBuild();

	// Trace logging with a flush per message is a real cost in a shipping game
	// and console noise the player did not ask for, so Dist raises the floor.
	// Warnings and errors must survive: they are what a shipped build needs.
	if (dist)
	{
		REQUIRE(Forge::Log::GetLogLevel() == spdlog::level::warn);
	}
	else
	{
		REQUIRE(Forge::Log::GetLogLevel() == spdlog::level::trace);
	}
}
