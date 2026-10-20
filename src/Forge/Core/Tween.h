#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Forge {

	// The shape of an easing curve (Godot's "trans").
	enum class TweenCurve : uint8_t
	{
		Linear,
		Sine,
		Quad,
		Cubic,
		Quart,
		Quint,
		Expo,
		Circ,
		Back,
		Bounce,
		Elastic,
	};

	// Which way the curve runs (Godot's "ease").
	enum class TweenEase : uint8_t
	{
		In,      // slow at the start
		Out,     // slow at the end
		InOut,   // slow at both ends
		OutIn,   // fast at the start and the end
	};

	// The value of a curve at a point: 0 at the start, 1 at the end.
	//
	// The curves are the ones Godot has, and they are pure functions of the
	// point: the same point always gives the same value, which is what makes
	// them testable and what makes a tween reproducible.
	[[nodiscard]] float EvaluateCurve(TweenCurve curve, TweenEase ease, float t);

	// A tween: a value that moves from where it is to where it should be.
	//
	// The value is written through a callback, so a tween cannot outlive the
	// thing it moves - the caller says what the value means.
	class Tween
	{
	public:
		using Apply = std::function<void(float)>;

		Tween(float from, float to, float duration, TweenCurve curve, TweenEase ease, Apply apply);

		// Advances the tween by dt and writes the value. False when the
		// tween has run out.
		bool Update(float dt);

		// Stops the tween where it is. The value stays where it is: a
		// tween that is cancelled does not jump to its target.
		void Cancel();

		[[nodiscard]] bool IsFinished() const { return m_Finished; }
		[[nodiscard]] bool IsCancelled() const { return m_Cancelled; }
		[[nodiscard]] float GetValue() const { return m_Value; }
		[[nodiscard]] float GetTarget() const { return m_To; }

		// How far along the tween is, from 0 to 1.
		[[nodiscard]] float GetProgress() const;

	private:
		float m_From = 0.0f;
		float m_To = 0.0f;
		float m_Duration = 0.0f;
		float m_Elapsed = 0.0f;
		float m_Value = 0.0f;
		TweenCurve m_Curve = TweenCurve::Linear;
		TweenEase m_Ease = TweenEase::InOut;
		Apply m_Apply;
		bool m_Finished = false;
		bool m_Cancelled = false;
	};

	// The tweens that are running.
	//
	// The service owns them: a tween that is added and then forgotten about
	// is a tween that keeps running, and a tween that outlives the thing it
	// moves is a crash. The caller decides when a tween is done with it.
	namespace Tweens {

		// Starts a tween. The service runs it until it is finished.
		void Add(Tween tween);

		// Advances every running tween and drops the ones that are done.
		void Update(float dt);

		// Stops every tween where it is.
		void Clear();

		// How many tweens are running.
		[[nodiscard]] size_t Count();

	}  // namespace Tweens

}  // namespace Forge
