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
//! tests: parse a declarative `transition = "fade 0.2"` string into a typed
//! spec, and turn that spec into a PLAN of which animation channels (alpha,
//! scale, positional offset) move from what to what over how long. No renderer,
//! no widget - the gui layer reads the plan and drives the channels through the
//! tween system, so enter/exit animation composes with layout instead of
//! fighting it. Modals and toasts reuse the same machinery.

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

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
		UTT_Pop			//!< scales up from zero with a slight overshoot
	};

	//! @brief a parsed transition: a family + a duration in seconds
	struct ORKIGE_CORE_DLL UiTransitionSpec
	{
		UiTransitionType	type = UTT_None;
		float				duration = 0.0f;

		inline bool isNone() const { return this->type == UTT_None; }
	};

	//! @brief the default transition duration (seconds) when a `transition` string
	//! names a family but omits the number
	extern ORKIGE_CORE_DLL const float UI_TRANSITION_DEFAULT_DURATION;

	//! @brief parse a declarative transition string ("fade 0.2", "slide-up 0.3",
	//! "pop", "none"). Case-insensitive; an unknown/empty family is UTT_None. A
	//! missing duration uses UI_TRANSITION_DEFAULT_DURATION. Underscores and
	//! hyphens both separate the direction ("slide_up" == "slide-up").
	ORKIGE_CORE_DLL UiTransitionSpec parseTransition(String const & text);

	//! @brief the script-facing family name (the inverse of parseTransition's
	//! family word); "none" for UTT_None
	ORKIGE_CORE_DLL const char* transitionTypeName(UiTransitionType type);

	//! @brief the concrete channel moves a transition performs, resolved for one
	//! direction (enter/exit). The gui layer reads whichever channels are active
	//! and tweens them; a rest widget sits at alpha 1, scale 1, offset (0,0).
	struct ORKIGE_CORE_DLL UiTransitionPlan
	{
		bool	animatesAlpha = false;
		float	alphaFrom = 1.0f;
		float	alphaTo = 1.0f;

		bool	animatesScale = false;
		float	scaleFrom = 1.0f;
		float	scaleTo = 1.0f;

		//! positional offset from the widget's resolved rest position, in pixels
		bool	animatesOffset = false;
		float	offsetFromX = 0.0f;
		float	offsetFromY = 0.0f;
		float	offsetToX = 0.0f;
		float	offsetToY = 0.0f;

		float		duration = 0.0f;
		String		ease;	//!< easing curve name (@see EaseLibrary::byName)
	};

	//! @brief build the channel plan for a transition. @param entering true for a
	//! show (rest is the END state), false for a hide (rest is the START state).
	//! @param slideDistanceX/Y how far a slide travels (the gui passes the widget
	//! extent or a sensible default). The exit reverses the enter so a hidden
	//! widget leaves the way it arrived.
	ORKIGE_CORE_DLL UiTransitionPlan planTransition(UiTransitionSpec const & spec,
		bool entering, float slideDistanceX, float slideDistanceY);
}

#endif //__UiTransition_h__11_7_2026__20_20_00__
