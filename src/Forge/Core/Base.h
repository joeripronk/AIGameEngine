#pragma once

#include "Forge/Core/PlatformDetection.h"

#include <memory>
#include <string>

#ifdef FG_DEBUG
	#if defined(FG_PLATFORM_WINDOWS)
		#define FG_DEBUGBREAK() __debugbreak()
	#elif defined(FG_PLATFORM_LINUX)
		#include <signal.h>
		#define FG_DEBUGBREAK() raise(SIGTRAP)
	#elif defined(FG_PLATFORM_MACOS)
		#include <signal.h>
		#define FG_DEBUGBREAK() raise(SIGTRAP)
	#else
		#error "Platform doesn't support debugbreak yet!"
	#endif
	#define FG_ENABLE_ASSERTS
#else
	#define FG_DEBUGBREAK()
#endif

#define FG_EXPAND_MACRO(x) x
#define FG_STRINGIFY_MACRO(x) #x

#define BIT(x) (1 << x)

#define FG_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace Forge {

	template<typename T>
	using Scope = std::unique_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Scope<T> CreateScope(Args&& ... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;
	template<typename T, typename ... Args>
	constexpr Ref<T> CreateRef(Args&& ... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

}

#include "Forge/Core/Log.h"
#include "Forge/Core/Assert.h"
