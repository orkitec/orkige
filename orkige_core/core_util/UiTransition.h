/********************************************************************
	created:	Saturday 2026/07/11 at 20:20
	filename: 	UiTransition.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __UiTransition_h__11_7_2026__20_20_00__
#define __UiTransition_h__11_7_2026__20_20_00__

//! @file UiTransition.h
//! @brief the pure show/hide transition vocabulary the gui shares with its unit
//! tests: parse a declarative `enter = fade 0.25 quadOut | slide 0 -40` string
//! into a typed spec, and turn that spec into a PLAN of which animation
//! channels (alpha, scale, positional offset) move from what to what over how
//! long and on which easing curve. No renderer, no widget, no tween library -
//! the gui layer reads the plan and drives the channels through the tween
//! system, so enter/exit animation composes with layout instead of fighting it.
//! Modals, toasts, tab panels and the screen router reuse the same machinery.
//!
//! GRAMMAR. A spec is one or more CLAUSES separated by `|`; each clause is
//! `family [numbers...] [easeName]`:
//!   fade [duration] [ease]                 opacity 0<->1
//!   pop [duration] [ease]                  scale 0<->1 (springy by default)
//!   slide-up|-down|-left|-right [d] [ease] slide in from that side, the travel
//!                                          taken from the widget's own extent
//!   slide dx dy [duration] [ease]          slide from an EXPLICIT pixel offset
//! Clauses of different families COMPOSE (they drive different channels, so
//! `fade 0.25 | slide 0 -40` fades and slides at once); two clauses of the same
//! channel are last-wins. `none` (or an empty string) is no animation at all.
//! Case-insensitive; `_` and `-` are interchangeable separators. An unknown
//! family is skipped with a diagnostic - never a screen that fails to build.

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <vector>

namespace Orkige
{
	//! @brief the enter/exit transition families. An exit plays the same family
	//! reversed (fade out, slide back out the way it came, pop down).
	enum UiTransitionType
	{
		UTT_None = 0,	//!< no animation (snap visible/hidden)
		UTT_Fade,		//!< opacity 0<->1
		UTT_SlideUp,	//!< enters sliding up from below its rest position
		UTT_SlideDown,	//!< enters sliding down from above its rest position
		UTT_SlideLeft,	//!< enters sliding left from right of its rest position
		UTT_SlideRight,	//!< enters sliding right from left of its rest position
		UTT_Pop,		//!< scales up from zero with a slight overshoot
		UTT_Slide		//!< slides in from an EXPLICIT pixel offset (dx, dy)
	};

	//! @brief one clause of a transition: a family, a duration, an optional
	//! explicit easing curve and - for UTT_Slide - the away offset in pixels
	struct ORKIGE_CORE_DLL UiTransitionClause
	{
		UiTransitionType	type = UTT_None;
		float				duration = 0.0f;
		//! the easing curve NAME (@see EaseLibrary::byName); empty = the
		//! family's own default (a pop overshoots, everything else eases out)
		String				ease;
		//! the away offset of an explicit `slide dx dy` (pixels, top-left-origin
		//! space: a negative y starts ABOVE the rest position)
		float				offsetX = 0.0f;
		float				offsetY = 0.0f;
	};

	//! @brief a parsed transition: the clauses that compose it, in order
	struct ORKIGE_CORE_DLL UiTransitionSpec
	{
		std::vector<UiTransitionClause>	clauses;

		//! @brief no animation at all (an unset / `none` / all-unknown spec)
		inline bool isNone() const { return this->clauses.empty(); }
		//! @brief the family of the first clause - the single-family readback
		//! (UTT_None when there is none)
		UiTransitionType primaryType() const;
		//! @brief the longest clause duration: how long the whole transition
		//! takes, which is what a caller waiting for it must wait for
		float totalDuration() const;
	};

	//! @brief the default transition duration (seconds) when a clause names a
	//! family but omits the number
	extern ORKIGE_CORE_DLL const float UI_TRANSITION_DEFAULT_DURATION;

	//! @brief parse a declarative transition string ("fade 0.2", "pop",
	//! "slide-up 0.3", "fade 0.25 quadOut | slide 0 -40", "none").
	//! Case-insensitive; underscores and hyphens both separate the direction
	//! ("slide_up" == "slide-up"). A missing duration uses
	//! UI_TRANSITION_DEFAULT_DURATION.
	//! @param diagnosticsOut optional: each malformed clause appends ONE
	//! human-readable line here (unknown family, an explicit `slide` missing its
	//! two numbers, a trailing token that is neither a number nor an ease name).
	//! A malformed clause is DROPPED and the rest of the spec still plays.
	ORKIGE_CORE_DLL UiTransitionSpec parseTransition(String const & text,
		std::vector<String> * diagnosticsOut = NULL);

	//! @brief the script-facing family name (the inverse of parseTransition's
	//! family word); "none" for UTT_None
	ORKIGE_CORE_DLL const char* transitionTypeName(UiTransitionType type);

	//! @brief the concrete channel moves a transition performs, resolved for one
	//! direction (enter/exit). The gui layer reads whichever channels are active
	//! and tweens each with its OWN duration and easing curve; a rest widget sits
	//! at alpha 1, scale 1, offset (0,0).
	struct ORKIGE_CORE_DLL UiTransitionPlan
	{
		bool	animatesAlpha = false;
		float	alphaFrom = 1.0f;
		float	alphaTo = 1.0f;
		float	alphaDuration = 0.0f;
		String	alphaEase;

		bool	animatesScale = false;
		float	scaleFrom = 1.0f;
		float	scaleTo = 1.0f;
		float	scaleDuration = 0.0f;
		String	scaleEase;

		//! positional offset from the widget's resolved rest position, in pixels
		bool	animatesOffset = false;
		float	offsetFromX = 0.0f;
		float	offsetFromY = 0.0f;
		float	offsetToX = 0.0f;
		float	offsetToY = 0.0f;
		float	offsetDuration = 0.0f;
		String	offsetEase;

		//! @brief how long the whole transition runs: its longest active channel.
		//! A caller that has to WAIT for a transition reads this; the gui itself
		//! drives each channel with its own duration above.
		float duration() const;
		//! @brief the first active channel's easing curve (@see
		//! EaseLibrary::byName) - the single-clause readback
		String const & ease() const;
	};

	//! @brief build the channel plan for a transition. @param entering true for a
	//! show (rest is the END state), false for a hide (rest is the START state).
	//! @param slideDistanceX/Y how far a DIRECTIONAL slide travels (the gui passes
	//! the widget extent or a sensible default; an explicit `slide dx dy` carries
	//! its own travel). The exit reverses the enter so a hidden widget leaves the
	//! way it arrived.
	ORKIGE_CORE_DLL UiTransitionPlan planTransition(UiTransitionSpec const & spec,
		bool entering, float slideDistanceX, float slideDistanceY);
}

#endif //__UiTransition_h__11_7_2026__20_20_00__
