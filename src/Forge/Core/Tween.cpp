#include "fgpch.h"
#include "Forge/Core/Tween.h"

#include <algorithm>
#include <cmath>

namespace Forge {

	namespace {

		constexpr float kPi = 3.14159265358979323846f;

		// The bounce: the value falls and bounces until it reaches the end.
		float BounceOut(float t);

		// The curves, as they are usually written: each one maps 0 to 0 and 1
		// to 1, and the "in" version starts slowly.
		float CurveIn(TweenCurve curve, float t)
		{
			switch (curve)
			{
			case TweenCurve::Sine:
				return 1.0f - std::cos(t * kPi * 0.5f);
			case TweenCurve::Quad:
				return t * t;
			case TweenCurve::Cubic:
				return t * t * t;
			case TweenCurve::Quart:
				return t * t * t * t;
			case TweenCurve::Quint:
				return t * t * t * t * t;
			case TweenCurve::Expo:
				return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
			case TweenCurve::Circ:
				return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
			case TweenCurve::Back:
			{
				// The overshoot: the curve goes past 1 and comes back.
				constexpr float c1 = 1.70158f;
				constexpr float c3 = c1 + 1.0f;
				return c3 * t * t * t - c1 * t * t;
			}
			case TweenCurve::Bounce:
				return 1.0f - BounceOut(1.0f - t);
			case TweenCurve::Elastic:
			{
				if (t == 0.0f || t == 1.0f)
					return t;
				constexpr float c4 = 2.0f * kPi / 3.0f;
				return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
			}
			case TweenCurve::Linear:
			default:
				return t;
			}
		}

		// The "out" curve is the "in" curve the other way round.
		float CurveOut(TweenCurve curve, float t)
		{
			return 1.0f - CurveIn(curve, 1.0f - t);
		}

		float BounceOut(float t)
		{
			constexpr float n1 = 7.5625f;
			constexpr float d1 = 2.75f;

			if (t < 1.0f / d1)
				return n1 * t * t;
			if (t < 2.0f / d1)
			{
				t -= 1.5f / d1;
				return n1 * t * t + 0.75f;
			}
			if (t < 2.5f / d1)
			{
				t -= 2.25f / d1;
				return n1 * t * t + 0.9375f;
			}
			t -= 2.625f / d1;
			return n1 * t * t + 0.984375f;
		}

	}  // namespace

	float EvaluateCurve(TweenCurve curve, TweenEase ease, float t)
	{
		// Outside the range the curve is clamped: a tween that is asked
		// about a time before it started or after it ended answers with the
		// value it holds there.
		t = std::clamp(t, 0.0f, 1.0f);

		switch (ease)
		{
		case TweenEase::In:
			return CurveIn(curve, t);
		case TweenEase::Out:
			return CurveOut(curve, t);
		case TweenEase::InOut:
			return t < 0.5f
				? 0.5f * CurveIn(curve, t * 2.0f)
				: 1.0f - 0.5f * CurveIn(curve, (1.0f - t) * 2.0f);
		case TweenEase::OutIn:
			return t < 0.5f
				? 0.5f * CurveOut(curve, t * 2.0f)
				: 1.0f - 0.5f * CurveOut(curve, (1.0f - t) * 2.0f);
		}
		return t;
	}

	Tween::Tween(float from, float to, float duration, TweenCurve curve, TweenEase ease, Apply apply)
		: m_From(from), m_To(to), m_Duration(std::max(0.0f, duration)),
		  m_Value(from), m_Curve(curve), m_Ease(ease), m_Apply(std::move(apply))
	{
		// A tween with no duration is over before it starts, and the value
		// it moves to is the one it holds.
		if (m_Duration <= 0.0f)
		{
			m_Value = m_To;
			m_Finished = true;
			if (m_Apply)
				m_Apply(m_Value);
		}
	}

	bool Tween::Update(float dt)
	{
		if (m_Finished || m_Cancelled)
			return false;

		m_Elapsed += dt;
		if (m_Elapsed >= m_Duration)
		{
			m_Elapsed = m_Duration;
			m_Value = m_To;
			m_Finished = true;
			if (m_Apply)
				m_Apply(m_Value);
			return false;
		}

		const float progress = m_Elapsed / m_Duration;
		const float eased = EvaluateCurve(m_Curve, m_Ease, progress);
		m_Value = m_From + (m_To - m_From) * eased;
		if (m_Apply)
			m_Apply(m_Value);
		return true;
	}

	void Tween::Cancel()
	{
		// A cancelled tween stops where it is: it does not jump to its
		// target.
		m_Cancelled = true;
	}

	float Tween::GetProgress() const
	{
		if (m_Duration <= 0.0f)
			return 1.0f;
		return std::clamp(m_Elapsed / m_Duration, 0.0f, 1.0f);
	}

	namespace Tweens {

		namespace {
			std::vector<Tween> s_Tweens;
		}

		void Add(Tween tween)
		{
			s_Tweens.push_back(std::move(tween));
		}

		void Update(float dt)
		{
			// The tweens that are done are dropped, and the ones that are
			// still running are kept. The list is walked backwards so a
			// tween that adds another one does not have it advanced in the
			// same frame.
			for (size_t i = s_Tweens.size(); i > 0; --i)
			{
				Tween& tween = s_Tweens[i - 1];
				if (!tween.Update(dt))
					s_Tweens.erase(s_Tweens.begin() + static_cast<long>(i - 1));
			}
		}

		void Clear()
		{
			s_Tweens.clear();
		}

		size_t Count()
		{
			return s_Tweens.size();
		}

	}  // namespace Tweens

}  // namespace Forge
