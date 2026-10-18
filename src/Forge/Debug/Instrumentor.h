#pragma once

// The Forge profiler: scoped instrumentation timers that stream Chrome-trace
// JSON (chrome://tracing, Perfetto), following the Hazel Instrumentor design.
//
// It is compiled in for Debug, Release, RelWithDebInfo and MinSizeRel, and
// compiled OUT of the shipping Dist build - a shipped game must not pay the
// clock reads and the mutex per instrumented scope.
//
// Usage:
//
//     FG_PROFILE_FUNCTION();               // times the whole function
//     FG_PROFILE_SCOPE("Load scene");      // times the enclosing block
//
//     GetInstrumentor().BeginSession("Main", "profile.json");   // [[nodiscard]]
//     ...
//     GetInstrumentor().EndSession();
//
// FG_PROFILE_FUNCTION() is the argumentless form: it is a macro named like a
// function call, and a self-referential macro expands to a call to an
// undeclared function.

#ifdef FG_ENABLE_PROFILING

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Forge {

	// A queryable fact, like the build-configuration queries: tells tools and
	// tests which branch the configuration compiled, without reading macros.
	inline bool IsProfilingEnabled() { return true; }

	// Per-scope aggregation (the Hazel profiler overlay panel's data): one
	// row per scope name, accumulated while a session is open and kept after
	// EndSession so the last session's numbers stay on screen.
	struct ProfileScopeStats
	{
		std::string Name;
		uint64_t Samples = 0;
		double TotalMillis = 0.0;
		double MinMillis = 0.0;
		double MaxMillis = 0.0;

		double MeanMillis() const { return Samples > 0 ? TotalMillis / static_cast<double>(Samples) : 0.0; }
	};

	struct ProfileSnapshot
	{
		uint64_t Samples = 0;

		// Sorted by TotalMillis, heaviest first - the panel's whole point is
		// answering "what is this frame spending time on" at a glance.
		std::vector<ProfileScopeStats> Scopes;
	};

	class Instrumentor
	{
	public:
		// Opens (truncating any previous file) and writes the trace header.
		// A session already in progress is closed first, so a file is never
		// left open across sessions and every session is valid JSON.
		// Returns false - without starting a session - when the file cannot
		// be opened.
		[[nodiscard]]
		bool BeginSession(const std::string& name, const std::string& filepath = "profile.json");

		void EndSession();

		// Predicate: safe to poll, safe to ignore.
		bool IsSessionActive() const { return m_SessionOpen; }

		// Writes one complete event. Used by InstrumentationTimer; calling
		// this directly requires the caller to own the start/end pair.
		void WriteProfile(const std::string& name, const std::string& category,
			int64_t startNs, int64_t endNs, std::thread::id threadID);

		// Nanoseconds from the monotonic clock, as the timer records them.
		static int64_t NowNs();

		// Aggregated per-scope timings for the live overlay panel. Aggregation
		// runs under the same session gate as the trace file: while profiling
		// is opt-in, nothing is recorded silently.
		ProfileSnapshot GetSnapshot() const;

		// Clears the aggregation. A predicate-free teardown like EndSession:
		// safe to call with no session.
		void ResetStats();

	private:
		void WriteHeader(const std::string& name, const std::string& filepath);

		std::ofstream m_Session;
		bool m_SessionOpen = false;
		uint32_t m_EventCount = 0;
		int64_t m_SessionStartNs = 0;
		uint64_t m_TotalSamples = 0;
		std::unordered_map<std::string, ProfileScopeStats> m_ScopeStats;
		mutable std::mutex m_Mutex;
	};

	// Defined below the inline member bodies.
	Instrumentor& GetInstrumentor();

	// Measures from construction to destruction and writes one event. A
	// timer created before any session is open writes nothing - the check
	// runs at destruction, when the answer is known.
	class InstrumentationTimer
	{
	public:
		explicit InstrumentationTimer(const std::string& name)
			: m_Name(name), m_StartNs(Instrumentor::NowNs())
		{
		}

		~InstrumentationTimer()
		{
			GetInstrumentor().WriteProfile(m_Name, "Function",
				m_StartNs, Instrumentor::NowNs(), std::this_thread::get_id());
		}

	private:
		std::string m_Name;
		int64_t m_StartNs;
	};

	inline Instrumentor& GetInstrumentor()
	{
		static Instrumentor instrumentor;
		return instrumentor;
	}

	// Session functions that exist in BOTH branches, so application code
	// does not need #ifdefs at every call site - the Dist branch reports
	// "no session" rather than removing the API. Begin is fallible in both,
	// so it is [[nodiscard]] in both.
	[[nodiscard]]
	inline bool ProfileBeginSession(const std::string& name, const std::string& filepath)
	{
		return GetInstrumentor().BeginSession(name, filepath);
	}

	inline void ProfileEndSession()
	{
		GetInstrumentor().EndSession();
	}

	// Predicate: polling it in UI code is normal.
	inline bool IsProfilingActive()
	{
		return GetInstrumentor().IsSessionActive();
	}

	// Overlay-panel access, present in both branches like the session API:
	// UI code needs no #ifdefs, and the Dist branch answers "no samples".
	inline ProfileSnapshot GetProfileSnapshot()
	{
		return GetInstrumentor().GetSnapshot();
	}

	inline void ResetProfileStats()
	{
		GetInstrumentor().ResetStats();
	}

	inline void Instrumentor::WriteHeader(const std::string& name, const std::string& filepath)
	{
		(void)name;
		(void)filepath;
		m_Session << "{\"traceEvents\":[";
	}

	inline bool Instrumentor::BeginSession(const std::string& name, const std::string& filepath)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		if (m_SessionOpen)
		{
			m_Session << "]}";
			m_Session.close();
			m_SessionOpen = false;
		}

		m_Session.open(filepath, std::ios::out | std::ios::trunc);
		if (!m_Session.is_open())
			return false;

		WriteHeader(name, filepath);
		m_SessionOpen = true;
		m_EventCount = 0;
		m_SessionStartNs = NowNs();

		// A new session measures from now; leftover numbers from the previous
		// one would read as this session's.
		m_ScopeStats.clear();
		m_TotalSamples = 0;
		return true;
	}

	inline void Instrumentor::EndSession()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (!m_SessionOpen)
			return;

		m_Session << "]}";
		m_Session.close();
		m_SessionOpen = false;
	}

	inline void Instrumentor::WriteProfile(const std::string& name, const std::string& category,
		int64_t startNs, int64_t endNs, std::thread::id threadID)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (!m_SessionOpen)
			return;

		if (m_EventCount > 0)
			m_Session << ",";

		const int64_t tid =
			static_cast<int64_t>(std::hash<std::thread::id>{}(threadID) & 0x7FFFFFFFFFFFFFFFull);
		const double ts = static_cast<double>(startNs - m_SessionStartNs) / 1000.0;
		const double dur = static_cast<double>(endNs - startNs) / 1000.0;

		m_Session << "{\"name\":\"" << name << "\",\"cat\":\"" << category
			<< "\",\"ph\":\"X\",\"pid\":1,\"tid\":" << tid
			<< ",\"ts\":" << ts << ",\"dur\":" << dur << "}";
		m_EventCount++;

		// Aggregate for the overlay panel. The trace file and the panel must
		// describe the same measurements, so this sits in the same gate.
		ProfileScopeStats& scope = m_ScopeStats[name];
		if (scope.Samples == 0)
		{
			scope.Name = name;
			scope.MinMillis = dur;
			scope.MaxMillis = dur;
		}
		else
		{
			scope.MinMillis = std::min(scope.MinMillis, dur);
			scope.MaxMillis = std::max(scope.MaxMillis, dur);
		}
		scope.Samples++;
		scope.TotalMillis += dur;
		m_TotalSamples++;
	}

	inline ProfileSnapshot Instrumentor::GetSnapshot() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);

		ProfileSnapshot snapshot;
		snapshot.Samples = m_TotalSamples;
		snapshot.Scopes.reserve(m_ScopeStats.size());
		for (const auto& entry : m_ScopeStats)
			snapshot.Scopes.push_back(entry.second);

		std::sort(snapshot.Scopes.begin(), snapshot.Scopes.end(),
			[](const ProfileScopeStats& a, const ProfileScopeStats& b)
			{ return a.TotalMillis > b.TotalMillis; });
		return snapshot;
	}

	inline void Instrumentor::ResetStats()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_ScopeStats.clear();
		m_TotalSamples = 0;
	}

	inline int64_t Instrumentor::NowNs()
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::high_resolution_clock::now().time_since_epoch()).count();
	}

}  // namespace Forge

#define FG_PROFILE_CONCAT_INNER(a, b) a##b
#define FG_PROFILE_CONCAT(a, b) FG_PROFILE_CONCAT_INNER(a, b)

// Times the named block until the end of its scope. The unique name keeps
// two scopes in one function from colliding on a single variable name.
#define FG_PROFILE_SCOPE(name) \
	Forge::InstrumentationTimer FG_PROFILE_CONCAT(fgProfileScope, __LINE__){ name }

#define FG_PROFILE_FUNCTION() \
	Forge::InstrumentationTimer FG_PROFILE_CONCAT(fgProfileScope, __LINE__){ __FUNCTION__ }

#define FG_PROFILE_BEGIN_SESSION(sessionName, filepath) \
	Forge::GetInstrumentor().BeginSession(sessionName, filepath)

#define FG_PROFILE_END_SESSION() \
	Forge::GetInstrumentor().EndSession()

#else

#include <cstdint>
#include <string>
#include <vector>

namespace Forge {
	inline bool IsProfilingEnabled() { return false; }

	// Same shapes as the profiling branch so UI code compiles and runs
	// identically in a shipping build: the panel shows "no samples", it does
	// not need to know it is in Dist.
	struct ProfileScopeStats
	{
		std::string Name;
		uint64_t Samples = 0;
		double TotalMillis = 0.0;
		double MinMillis = 0.0;
		double MaxMillis = 0.0;

		double MeanMillis() const { return Samples > 0 ? TotalMillis / static_cast<double>(Samples) : 0.0; }
	};

	struct ProfileSnapshot
	{
		uint64_t Samples = 0;
		std::vector<ProfileScopeStats> Scopes;
	};

	[[nodiscard]]
	inline bool ProfileBeginSession(const std::string&, const std::string&) { return false; }
	inline void ProfileEndSession() {}
	inline bool IsProfilingActive() { return false; }
	inline ProfileSnapshot GetProfileSnapshot() { return {}; }
	inline void ResetProfileStats() {}
}

#define FG_PROFILE_SCOPE(name)             ((void)0)
#define FG_PROFILE_FUNCTION()              ((void)0)
#define FG_PROFILE_BEGIN_SESSION(a, b)     ((void)0)
#define FG_PROFILE_END_SESSION()           ((void)0)

#endif  // FG_ENABLE_PROFILING
