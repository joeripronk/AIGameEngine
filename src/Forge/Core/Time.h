#pragma once

#include <functional>

namespace Forge {

	// The engine's frame clock.
	//
	// Every "how long since" question in the engine - the frame loop's delta
	// time, `Timer`, the Lua `GetTime()` global - reads one function instead of
	// calling the windowing library directly. Two things follow from that. Core
	// no longer depends on a windowing library for its notion of time, so a
	// headless tool can run it; and the clock can be replaced, which is what
	// makes a frame loop testable at all: a test drives a fixed sequence of
	// timestamps and gets a deterministic delta time instead of whatever the
	// machine happened to take.
	namespace Time {

		// Seconds since an unspecified fixed origin, monotonic within a session.
		[[nodiscard]]
		double GetTime();

		// Installs a clock. Passing an empty function restores the default
		// (std::chrono::steady_clock). Only the owning thread should swap the
		// clock, and a frame loop must not be running while it does.
		void SetSource(std::function<double()> source);

	}  // namespace Time

}  // namespace Forge