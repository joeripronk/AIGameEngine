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

		static Ref<spdlog::logger>& GetCoreLogger() { return s_CoreLogger; }
		static Ref<spdlog::logger>& GetClientLogger() { return s_ClientLogger; }
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
#define FG_ASSERT(x, ...) do { if(!(x)) { FG_CORE_ERROR(__VA_ARGS__); FG_DEBUGBREAK(); } } while(0)
#define FG_CORE_ASSERT(x) do { if(!(x)) { FG_CORE_ERROR("Assertion failed: " #x); FG_DEBUGBREAK(); } } while(0)
#define FG_CORE_ASSERT_MSG(x, msg, ...) do { if(!(x)) { FG_CORE_ERROR(msg, ##__VA_ARGS__); FG_DEBUGBREAK(); } } while(0)

// Client log macros
#define FG_TRACE(...)         ::Forge::Log::GetClientLogger()->trace(__VA_ARGS__)
#define FG_INFO(...)          ::Forge::Log::GetClientLogger()->info(__VA_ARGS__)
#define FG_WARN(...)          ::Forge::Log::GetClientLogger()->warn(__VA_ARGS__)
#define FG_ERROR(...)         ::Forge::Log::GetClientLogger()->error(__VA_ARGS__)
#define FG_CRITICAL(...)      ::Forge::Log::GetClientLogger()->critical(__VA_ARGS__)
