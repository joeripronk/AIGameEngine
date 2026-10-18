#pragma once

#include "Forge/Core/Base.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp"

// This ignores all warnings raised inside External headers
#if defined(_MSC_VER)
	#pragma warning(push, 0)
#endif
#include <spdlog/spdlog.h>
#include <spdlog/fmt/fmt.h>
#if defined(_MSC_VER)
	#pragma warning(pop)
#endif

namespace Forge {

	class Log
	{
	public:
		static void Init();

		// Drops both loggers. An application calls this at shutdown; a test
		// calls it to observe the uninitialised state.
		//
		// It exists because the log macros are guarded rather than assumed:
		// engine code logs from constructors and static initialisers, which
		// can and do run before Init, and a headless tool that links ForgeCore
		// may never call Init at all.
		static void Shutdown();

		static Ref<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static Ref<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }

		// The level the loggers were initialised at.
		//
		// A shipping build raises this to warn: trace-level logging that flushes
		// on every message is both a large I/O cost and console spew the player
		// should not be reading. Warnings and errors still get through, which is
		// what a shipped game actually needs.
		[[nodiscard]] static spdlog::level::level_enum GetLogLevel();
	private:
		static Ref<spdlog::logger> s_CoreLogger;
		static Ref<spdlog::logger> s_ClientLogger;
	};

}

template<typename OStream, glm::length_t L, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::vec<L, T, Q>& vector)
{
	return os << glm::to_string(vector);
}

template<typename OStream, glm::length_t C, glm::length_t R, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, const glm::mat<C, R, T, Q>& matrix)
{
	return os << glm::to_string(matrix);
}

template<typename OStream, typename T, glm::qualifier Q>
inline OStream& operator<<(OStream& os, glm::qua<T, Q> quaternion)
{
	return os << glm::to_string(quaternion);
}

// Core log macros
#define FG_CORE_TRACE(...)    (::Forge::Log::GetCoreLogger() ? ::Forge::Log::GetCoreLogger()->trace(__VA_ARGS__) : (void)0)
#define FG_CORE_INFO(...)     (::Forge::Log::GetCoreLogger() ? ::Forge::Log::GetCoreLogger()->info(__VA_ARGS__) : (void)0)
#define FG_CORE_WARN(...)     (::Forge::Log::GetCoreLogger() ? ::Forge::Log::GetCoreLogger()->warn(__VA_ARGS__) : (void)0)
#define FG_CORE_ERROR(...)    (::Forge::Log::GetCoreLogger() ? ::Forge::Log::GetCoreLogger()->error(__VA_ARGS__) : (void)0)
#define FG_CORE_CRITICAL(...) (::Forge::Log::GetCoreLogger() ? ::Forge::Log::GetCoreLogger()->critical(__VA_ARGS__) : (void)0)

// Assert macros
//
// These are a debug facility, gated on FG_ENABLE_ASSERTS (which Base.h derives
// from FG_DEBUG). Defining them unconditionally meant a shipped build still
// evaluated every invariant and logged it, so BuildConfig::AreAssertsEnabled
// would have been reporting on a macro definition rather than on behaviour.
//
// The condition is not evaluated when asserts are off, so a variable used only
// inside an assert can become unused in a shipping build. Declare it
// [[maybe_unused]] if that happens - with -Werror it is a build break rather
// than a silent problem.
#ifdef FG_ENABLE_ASSERTS
	#define FG_ASSERT(x, ...) do { if(!(x)) { FG_CORE_ERROR(__VA_ARGS__); FG_DEBUGBREAK(); } } while(0)
	#define FG_CORE_ASSERT(x) do { if(!(x)) { FG_CORE_ERROR("Assertion failed: " #x); FG_DEBUGBREAK(); } } while(0)
	#define FG_CORE_ASSERT_MSG(x, msg, ...) do { if(!(x)) { FG_CORE_ERROR(msg); FG_DEBUGBREAK(); } } while(0)
#else
	#define FG_ASSERT(...) ((void)0)
	#define FG_CORE_ASSERT(...) ((void)0)
	#define FG_CORE_ASSERT_MSG(...) ((void)0)
#endif

// Client log macros
//
// Guarded exactly like the core macros, and for the same reason. They were
// not: anything logging before Log::Init - an application constructor, a
// static initialiser, a headless tool that never initialises logging at all -
// dereferenced a null logger and took the process down. A failure path is the
// last place that should crash, and it crashed precisely when it was used:
// `EditorContext::SaveProject` reporting "no project is open" killed the test
// that called it.
#define FG_TRACE(...)         (::Forge::Log::GetClientLogger() ? ::Forge::Log::GetClientLogger()->trace(__VA_ARGS__) : (void)0)
#define FG_INFO(...)          (::Forge::Log::GetClientLogger() ? ::Forge::Log::GetClientLogger()->info(__VA_ARGS__) : (void)0)
#define FG_WARN(...)          (::Forge::Log::GetClientLogger() ? ::Forge::Log::GetClientLogger()->warn(__VA_ARGS__) : (void)0)
#define FG_ERROR(...)         (::Forge::Log::GetClientLogger() ? ::Forge::Log::GetClientLogger()->error(__VA_ARGS__) : (void)0)
#define FG_CRITICAL(...)      (::Forge::Log::GetClientLogger() ? ::Forge::Log::GetClientLogger()->critical(__VA_ARGS__) : (void)0)
