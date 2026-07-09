/**************************************************************
	created:	2026/07/09 at 10:00
	filename: 	EaseLibrary.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EaseLibrary_h__9_7_2026__10_00_00__
#define __EaseLibrary_h__9_7_2026__10_00_00__

#include "core_util/String.h"

#include <cmath>

namespace Orkige
{
	/** \addtogroup Tween
	*  @{ */
	//! @brief the shared easing curve library: pure functions mapping a
	//! normalized time t in [0,1] to a normalized progress value, with the
	//! invariants f(0) = 0 and f(1) = 1 (back/elastic over/undershoot in
	//! between by design; bounce stays in [0,1]).
	//! @remarks deliberately a standalone, dependency-free header: the tween
	//! system (core_tween/TweenManager.h) drives object properties through
	//! these curves and the particle system consumes them for over-life
	//! curves - both share this one vocabulary. The names follow the
	//! Penner convention: <family><In|Out|InOut>, e.g. "quadOut";
	//! Ease::byName resolves the script-facing string names.
	namespace Ease
	{
		//! an easing curve: normalized time -> normalized progress
		typedef float (*Function)(float);

		//--- linear ------------------------------------------------
		inline float linear(float t)		{ return t; }

		//--- quad (t^2) --------------------------------------------
		inline float quadIn(float t)		{ return t * t; }
		inline float quadOut(float t)		{ return 1.0f - (1.0f - t) * (1.0f - t); }
		inline float quadInOut(float t)
		{
			return t < 0.5f ? 2.0f * t * t
				: 1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f) / 2.0f;
		}

		//--- cubic (t^3) -------------------------------------------
		inline float cubicIn(float t)		{ return t * t * t; }
		inline float cubicOut(float t)
		{
			const float u = 1.0f - t;
			return 1.0f - u * u * u;
		}
		inline float cubicInOut(float t)
		{
			if(t < 0.5f)
			{
				return 4.0f * t * t * t;
			}
			const float u = -2.0f * t + 2.0f;
			return 1.0f - u * u * u / 2.0f;
		}

		//--- quart (t^4) -------------------------------------------
		inline float quartIn(float t)		{ return t * t * t * t; }
		inline float quartOut(float t)
		{
			const float u = 1.0f - t;
			return 1.0f - u * u * u * u;
		}
		inline float quartInOut(float t)
		{
			if(t < 0.5f)
			{
				return 8.0f * t * t * t * t;
			}
			const float u = -2.0f * t + 2.0f;
			return 1.0f - u * u * u * u / 2.0f;
		}

		//--- quint (t^5) -------------------------------------------
		inline float quintIn(float t)		{ return t * t * t * t * t; }
		inline float quintOut(float t)
		{
			const float u = 1.0f - t;
			return 1.0f - u * u * u * u * u;
		}
		inline float quintInOut(float t)
		{
			if(t < 0.5f)
			{
				return 16.0f * t * t * t * t * t;
			}
			const float u = -2.0f * t + 2.0f;
			return 1.0f - u * u * u * u * u / 2.0f;
		}

		//--- sine --------------------------------------------------
		inline float sineIn(float t)
		{
			return 1.0f - std::cos(t * 3.14159265358979f / 2.0f);
		}
		inline float sineOut(float t)
		{
			return std::sin(t * 3.14159265358979f / 2.0f);
		}
		inline float sineInOut(float t)
		{
			return -(std::cos(3.14159265358979f * t) - 1.0f) / 2.0f;
		}

		//--- expo (2^x) - endpoints handled exactly ----------------
		inline float expoIn(float t)
		{
			return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f * t - 10.0f);
		}
		inline float expoOut(float t)
		{
			return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
		}
		inline float expoInOut(float t)
		{
			if(t <= 0.0f)
			{
				return 0.0f;
			}
			if(t >= 1.0f)
			{
				return 1.0f;
			}
			return t < 0.5f ? std::pow(2.0f, 20.0f * t - 10.0f) / 2.0f
				: (2.0f - std::pow(2.0f, -20.0f * t + 10.0f)) / 2.0f;
		}

		//--- circ (quarter circle) ---------------------------------
		inline float circIn(float t)
		{
			return 1.0f - std::sqrt(1.0f - t * t);
		}
		inline float circOut(float t)
		{
			return std::sqrt(1.0f - (t - 1.0f) * (t - 1.0f));
		}
		inline float circInOut(float t)
		{
			return t < 0.5f
				? (1.0f - std::sqrt(1.0f - 4.0f * t * t)) / 2.0f
				: (std::sqrt(1.0f - (-2.0f * t + 2.0f) * (-2.0f * t + 2.0f)) + 1.0f) / 2.0f;
		}

		//--- back (overshoots) -------------------------------------
		inline float backIn(float t)
		{
			const float c1 = 1.70158f;
			const float c3 = c1 + 1.0f;
			return c3 * t * t * t - c1 * t * t;
		}
		inline float backOut(float t)
		{
			const float c1 = 1.70158f;
			const float c3 = c1 + 1.0f;
			const float u = t - 1.0f;
			return 1.0f + c3 * u * u * u + c1 * u * u;
		}
		inline float backInOut(float t)
		{
			const float c1 = 1.70158f;
			const float c2 = c1 * 1.525f;
			if(t < 0.5f)
			{
				const float u = 2.0f * t;
				return (u * u * ((c2 + 1.0f) * u - c2)) / 2.0f;
			}
			const float u = 2.0f * t - 2.0f;
			return (u * u * ((c2 + 1.0f) * u + c2) + 2.0f) / 2.0f;
		}

		//--- elastic (oscillates) - endpoints handled exactly ------
		inline float elasticIn(float t)
		{
			const float c4 = 2.0f * 3.14159265358979f / 3.0f;
			if(t <= 0.0f)
			{
				return 0.0f;
			}
			if(t >= 1.0f)
			{
				return 1.0f;
			}
			return -std::pow(2.0f, 10.0f * t - 10.0f) *
				std::sin((t * 10.0f - 10.75f) * c4);
		}
		inline float elasticOut(float t)
		{
			const float c4 = 2.0f * 3.14159265358979f / 3.0f;
			if(t <= 0.0f)
			{
				return 0.0f;
			}
			if(t >= 1.0f)
			{
				return 1.0f;
			}
			return std::pow(2.0f, -10.0f * t) *
				std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
		}
		inline float elasticInOut(float t)
		{
			const float c5 = 2.0f * 3.14159265358979f / 4.5f;
			if(t <= 0.0f)
			{
				return 0.0f;
			}
			if(t >= 1.0f)
			{
				return 1.0f;
			}
			return t < 0.5f
				? -(std::pow(2.0f, 20.0f * t - 10.0f) *
					std::sin((20.0f * t - 11.125f) * c5)) / 2.0f
				: (std::pow(2.0f, -20.0f * t + 10.0f) *
					std::sin((20.0f * t - 11.125f) * c5)) / 2.0f + 1.0f;
		}

		//--- bounce ------------------------------------------------
		inline float bounceOut(float t)
		{
			const float n1 = 7.5625f;
			const float d1 = 2.75f;
			if(t < 1.0f / d1)
			{
				return n1 * t * t;
			}
			if(t < 2.0f / d1)
			{
				t -= 1.5f / d1;
				return n1 * t * t + 0.75f;
			}
			if(t < 2.5f / d1)
			{
				t -= 2.25f / d1;
				return n1 * t * t + 0.9375f;
			}
			t -= 2.625f / d1;
			return n1 * t * t + 0.984375f;
		}
		inline float bounceIn(float t)
		{
			return 1.0f - bounceOut(1.0f - t);
		}
		inline float bounceInOut(float t)
		{
			return t < 0.5f
				? (1.0f - bounceOut(1.0f - 2.0f * t)) / 2.0f
				: (1.0f + bounceOut(2.0f * t - 1.0f)) / 2.0f;
		}

		//! @brief resolve an easing curve by its script-facing name
		//! ("linear", "quadOut", "backInOut", "bounceOut", ...)
		//! @return the function, or NULL for an unknown name (callers decide
		//! their fallback - the Lua binding logs and falls back to linear)
		inline Function byName(String const & name)
		{
			struct NamedEase
			{
				char const *	mName;
				Function		mFunction;
			};
			static const NamedEase eases[] =
			{
				{ "linear",			&linear },
				{ "quadIn",			&quadIn },		{ "quadOut",	&quadOut },		{ "quadInOut",		&quadInOut },
				{ "cubicIn",		&cubicIn },		{ "cubicOut",	&cubicOut },	{ "cubicInOut",		&cubicInOut },
				{ "quartIn",		&quartIn },		{ "quartOut",	&quartOut },	{ "quartInOut",		&quartInOut },
				{ "quintIn",		&quintIn },		{ "quintOut",	&quintOut },	{ "quintInOut",		&quintInOut },
				{ "sineIn",			&sineIn },		{ "sineOut",	&sineOut },		{ "sineInOut",		&sineInOut },
				{ "expoIn",			&expoIn },		{ "expoOut",	&expoOut },		{ "expoInOut",		&expoInOut },
				{ "circIn",			&circIn },		{ "circOut",	&circOut },		{ "circInOut",		&circInOut },
				{ "backIn",			&backIn },		{ "backOut",	&backOut },		{ "backInOut",		&backInOut },
				{ "elasticIn",		&elasticIn },	{ "elasticOut",	&elasticOut },	{ "elasticInOut",	&elasticInOut },
				{ "bounceIn",		&bounceIn },	{ "bounceOut",	&bounceOut },	{ "bounceInOut",	&bounceInOut },
			};
			for(NamedEase const & entry : eases)
			{
				if(name == entry.mName)
				{
					return entry.mFunction;
				}
			}
			return 0;
		}
	}
	/** @} */
}

#endif //__EaseLibrary_h__9_7_2026__10_00_00__
