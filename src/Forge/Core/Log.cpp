#include "fgpch.h"
#include "Forge/Core/Log.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace Forge {

	Ref<spdlog::logger> Log::s_CoreLogger;
	Ref<spdlog::logger> Log::s_ClientLogger;

	namespace {

		// The level both loggers are initialised at.
		//
		// Development builds want everything. A distribution build does not: the
		// engine logs a great deal per frame, and at trace with a flush per
		// message that is a measurable I/O cost in a shipping game plus a console
		// full of noise the player did not ask for. Warnings and errors survive,
		// which is what is actually useful once the game is in front of someone.
		spdlog::level::level_enum ConfiguredLogLevel()
		{
		#if defined(FG_DIST)
			return spdlog::level::warn;
		#else
			return spdlog::level::trace;
		#endif
		}

	} // namespace

	spdlog::level::level_enum Log::GetLogLevel()
	{
		return ConfiguredLogLevel();
	}

	void Log::Shutdown()
	{
		// Drop through spdlog's registry first, so a later Init() re-registers
		// cleanly instead of hitting the "already exists" early-out with a
		// logger whose sinks have already been torn down.
		if (s_CoreLogger)
		{
			spdlog::drop(s_CoreLogger->name());
			s_CoreLogger = nullptr;
		}

		if (s_ClientLogger)
		{
			spdlog::drop(s_ClientLogger->name());
			s_ClientLogger = nullptr;
		}
	}

	void Log::Init()
	{
		// Check if loggers already exist (idempotent initialization)
		if (spdlog::get("FORGE"))
			return;

		std::vector<spdlog::sink_ptr> logSinks;
		logSinks.emplace_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
		logSinks.emplace_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>("Forge.log", true));

		logSinks[0]->set_pattern("%^[%T] %n: %v%$");
		logSinks[1]->set_pattern("[%T] [%l] %n: %v");

		const spdlog::level::level_enum level = ConfiguredLogLevel();

		s_CoreLogger = std::make_shared<spdlog::logger>("FORGE", begin(logSinks), end(logSinks));
		spdlog::register_logger(s_CoreLogger);
		s_CoreLogger->set_level(level);
		s_CoreLogger->flush_on(level);

		s_ClientLogger = std::make_shared<spdlog::logger>("APP", begin(logSinks), end(logSinks));
		spdlog::register_logger(s_ClientLogger);
		s_ClientLogger->set_level(level);
		s_ClientLogger->flush_on(level);
	}

}
