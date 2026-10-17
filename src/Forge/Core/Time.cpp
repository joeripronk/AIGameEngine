#include "fgpch.h"
#include "Forge/Core/Base.h"
#include "Forge/Core/Time.h"

namespace Forge::Time {

	namespace {

		double DefaultTime()
		{
			// steady_clock, not the windowing library's timer: the frame clock
			// must exist with no window (headless tools, the editor's
			// deterministic stepping), and must never jump backwards when the
			// wall clock is adjusted.
			using namespace std::chrono;
			return duration<double>(steady_clock::now().time_since_epoch()).count();
		}

		std::function<double()>& Source()
		{
			// Function-local statics: initialised on first use and thread-safe,
			// which a namespace-scope function object with a non-trivial
			// constructor is not.
			static std::function<double()> s_Source;
			return s_Source;
		}

	}  // namespace

	double GetTime()
	{
		if (const std::function<double()>& source = Source(); source)
			return source();
		return DefaultTime();
	}

	void SetSource(std::function<double()> source)
	{
		Source() = std::move(source);
	}

}  // namespace Forge::Time