#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "Forge/Debug/Instrumentor.h"
#include "Forge/Core/BuildConfig.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

	std::string ScratchPath(const std::string& name)
	{
		return (std::filesystem::path("test_profile_scratch") / name).string();
	}

	struct ScratchCleaner
	{
		ScratchCleaner()
		{
			std::error_code ec;
			std::filesystem::remove_all("test_profile_scratch", ec);
			std::filesystem::create_directories("test_profile_scratch", ec);
		}
		~ScratchCleaner()
		{
			std::error_code ec;
			std::filesystem::remove_all("test_profile_scratch", ec);
		}
	};

	std::string ReadAll(const std::string& path)
	{
		std::ifstream in(path);
		return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	}

	size_t CountOccurrences(const std::string& haystack, const std::string& needle)
	{
		size_t count = 0;
		size_t pos = 0;
		while ((pos = haystack.find(needle, pos)) != std::string::npos)
		{
			++count;
			pos += needle.size();
		}
		return count;
	}

}  // namespace

TEST_CASE("Profiling follows the build configuration", "[core][profiler]")
{
	// The shipping build must not carry instrumentation at all; every other
	// configuration records. Same canary role as BuildConfigTests: a
	// mis-gated configuration is a test failure, not a silent surprise.
	if (Forge::IsDistributionBuild())
		REQUIRE_FALSE(Forge::IsProfilingEnabled());
	else
		REQUIRE(Forge::IsProfilingEnabled());
}

TEST_CASE("Profiler sessions outside the enabled branch are inert", "[core][profiler]")
{
	ScratchCleaner cleaner;
	const std::string path = ScratchPath("inert.json");

	// The session API exists in both branches; the Dist branch reports
	// failure and never creates a file.
	const bool started = Forge::ProfileBeginSession("Inert", path);
	REQUIRE(started == Forge::IsProfilingEnabled());

	Forge::ProfileEndSession();

	if (!Forge::IsProfilingEnabled())
	{
		std::error_code ec;
		REQUIRE_FALSE(std::filesystem::exists(path, ec));
	}
}

TEST_CASE("A profile session writes one Chrome-trace document", "[core][profiler]")
{
	if (!Forge::IsProfilingEnabled())
		return;

	ScratchCleaner cleaner;
	const std::string path = ScratchPath("one.json");

	REQUIRE(Forge::ProfileBeginSession("One Scope", path));

	{
		FG_PROFILE_SCOPE("Measured Work");
		volatile double sink = 0.0;
		for (int i = 0; i < 1000; ++i)
			sink += i;
		(void)sink;
	}

	Forge::ProfileEndSession();

	const std::string json = ReadAll(path);

	REQUIRE(json.starts_with("{\"traceEvents\":["));
	REQUIRE(json.ends_with("]}"));

	// Exactly one complete event, and the document closes exactly once -
	// the old implementation closed the array twice, producing "]}]}" that
	// no trace viewer accepts.
	REQUIRE(CountOccurrences(json, "\"ph\":\"X\"") == 1u);
	REQUIRE(CountOccurrences(json, "]}") == 1u);
	REQUIRE(json.find("Measured Work") != std::string::npos);
	REQUIRE(json.find("\"cat\":\"Function\"") != std::string::npos);
}

TEST_CASE("Nested scopes are recorded as separate events", "[core][profiler]")
{
	if (!Forge::IsProfilingEnabled())
		return;

	ScratchCleaner cleaner;
	const std::string path = ScratchPath("nested.json");

	REQUIRE(Forge::ProfileBeginSession("Nested", path));

	{
		FG_PROFILE_SCOPE("Outer");
		{
			FG_PROFILE_SCOPE("Inner");
		}
	}

	Forge::ProfileEndSession();

	const std::string json = ReadAll(path);
	REQUIRE(CountOccurrences(json, "\"ph\":\"X\"") == 2u);
	REQUIRE(json.find("Outer") != std::string::npos);
	REQUIRE(json.find("Inner") != std::string::npos);
}

TEST_CASE("Scopes outside a session write nothing", "[core][profiler]")
{
	if (!Forge::IsProfilingEnabled())
		return;

	ScratchCleaner cleaner;
	const std::string path = ScratchPath("quiet.json");

	{
		FG_PROFILE_SCOPE("Before any session");
	}

	REQUIRE(Forge::ProfileBeginSession("Quiet", path));
	{
		FG_PROFILE_SCOPE("Inside the session");
	}
	Forge::ProfileEndSession();

	// The pre-session scope must not leak into the trace: the timer
	// checks the session at destruction, when the answer is known.
	const std::string json = ReadAll(path);
	REQUIRE(CountOccurrences(json, "\"ph\":\"X\"") == 1u);
	REQUIRE(json.find("Before any session") == std::string::npos);
}

TEST_CASE("A second session truncates the previous trace", "[core][profiler]")
{
	if (!Forge::IsProfilingEnabled())
		return;

	ScratchCleaner cleaner;
	const std::string path = ScratchPath("twice.json");

	REQUIRE(Forge::ProfileBeginSession("First", path));
	{
		FG_PROFILE_SCOPE("First Event");
	}
	// BeginSession while a session is open closes it and reopens the file
	// from scratch - no concatenated half-documents.
	REQUIRE(Forge::ProfileBeginSession("Second", path));
	{
		FG_PROFILE_SCOPE("Second Event");
	}
	Forge::ProfileEndSession();

	const std::string json = ReadAll(path);
	REQUIRE(CountOccurrences(json, "\"ph\":\"X\"") == 1u);
	REQUIRE(json.find("First Event") == std::string::npos);
	REQUIRE(json.find("Second Event") != std::string::npos);
	REQUIRE(CountOccurrences(json, "]}") == 1u);
}

TEST_CASE("Concurrent scopes are all recorded", "[core][profiler]")
{
	if (!Forge::IsProfilingEnabled())
		return;

	ScratchCleaner cleaner;
	const std::string path = ScratchPath("threads.json");

	REQUIRE(Forge::ProfileBeginSession("Threads", path));

	std::vector<std::thread> threads;
	for (int i = 0; i < 4; ++i)
	{
		threads.emplace_back([]
		{
			FG_PROFILE_SCOPE("Worker");
			volatile double sink = 0.0;
			for (int k = 0; k < 10000; ++k)
				sink += k;
			(void)sink;
		});
	}
	for (auto& t : threads)
		t.join();

	Forge::ProfileEndSession();

	const std::string json = ReadAll(path);
	REQUIRE(CountOccurrences(json, "\"ph\":\"X\"") == 4u);

	// The mutex must serialise whole events, not bytes: a torn write shows
	// up as an unbalanced document.
	REQUIRE(CountOccurrences(json, "{") == CountOccurrences(json, "}"));
}

TEST_CASE("BeginSession reports failure for an unopenable path", "[core][profiler]")
{
	ScratchCleaner cleaner;
	const std::string path = ScratchPath("missing_dir/deep.json");

	REQUIRE_FALSE(Forge::ProfileBeginSession("Unopenable", path));
	REQUIRE_FALSE(Forge::IsProfilingActive());

	// EndSession with no session is a no-op, not a crash.
	Forge::ProfileEndSession();
}
