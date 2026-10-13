#pragma once

#include "Forge/Core/Base.h"
#include "Forge/Core/Log.h"
#include <filesystem>

#ifdef FG_ENABLE_ASSERTS

	// Alteratively we could use the same "default" message for both "WITH_MSG" and "NO_MSG" and
	// provide support for custom formatting by concatenating the formatting string instead of having the format inside the default message
	#define FG_INTERNAL_ASSERT_IMPL(type, check, msg, ...) { if(!(check)) { FG##type##ERROR(msg, __VA_ARGS__); FG_DEBUGBREAK(); } }
	#define FG_INTERNAL_ASSERT_WITH_MSG(type, check, ...) FG_INTERNAL_ASSERT_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
	#define FG_INTERNAL_ASSERT_NO_MSG(type, check) FG_INTERNAL_ASSERT_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", FG_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

	#define FG_INTERNAL_ASSERT_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
	#define FG_INTERNAL_ASSERT_GET_MACRO(...) FG_EXPAND_MACRO( FG_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, FG_INTERNAL_ASSERT_WITH_MSG, FG_INTERNAL_ASSERT_NO_MSG) )

	// Currently accepts at least the condition and one additional parameter (the message) being optional
	#define FG_ASSERT(...) FG_EXPAND_MACRO( FG_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_, __VA_ARGS__) )
	#define FG_CORE_ASSERT(...) FG_EXPAND_MACRO( FG_INTERNAL_ASSERT_GET_MACRO(_CORE_, __VA_ARGS__) )
#else
	#define FG_ASSERT(...)
	#define FG_CORE_ASSERT(...)
#endif
