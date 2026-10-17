#pragma once

#include "Forge/Core/Time.h"

namespace Forge {

	// Elapsed-time helper (the Hazel Timer).
	//
	// It reads `Time::GetTime()` rather than its own steady_clock, so a timer
	// follows whatever the engine's clock is - which is what lets a test
	// measure a timer exactly, instead of sleeping and hoping.
	class Timer
	{
	public:
		Timer()
		{
			Reset();
		}

		void Reset()
		{
			m_Start = Time::GetTime();
		}

		[[nodiscard]]
		float Elapsed() const
		{
			return static_cast<float>(Time::GetTime() - m_Start);
		}

		[[nodiscard]]
		float ElapsedMillis() const
		{
			return Elapsed() * 1000.0f;
		}

	private:
		double m_Start = 0.0;
	};

}  // namespace Forge