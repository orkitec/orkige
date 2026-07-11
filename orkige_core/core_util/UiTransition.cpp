/********************************************************************
	created:	Saturday 2026/07/11 at 20:20
	filename: 	UiTransition.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_util/UiTransition.h"
#include "core_util/StringUtil.h"

#include <sstream>

namespace Orkige
{
	const float UI_TRANSITION_DEFAULT_DURATION = 0.2f;
	//---------------------------------------------------------
	UiTransitionSpec parseTransition(String const & text)
	{
		UiTransitionSpec spec;
		// split into a family word and an optional duration number
		std::istringstream stream(text);
		String family;
		stream >> family;
		if(family.empty())
		{
			return spec;	// UTT_None
		}
		StringUtil::to_lower(family);
		// hyphen/underscore are interchangeable separators
		for(char & c : family)
		{
			if(c == '_')
			{
				c = '-';
			}
		}

		if(family == "none")			spec.type = UTT_None;
		else if(family == "fade")		spec.type = UTT_Fade;
		else if(family == "slide-up")	spec.type = UTT_SlideUp;
		else if(family == "slide-down")	spec.type = UTT_SlideDown;
		else if(family == "slide-left")	spec.type = UTT_SlideLeft;
		else if(family == "slide-right")spec.type = UTT_SlideRight;
		else if(family == "pop")		spec.type = UTT_Pop;
		else							spec.type = UTT_None;

		if(spec.type == UTT_None)
		{
			return spec;
		}
		float duration = UI_TRANSITION_DEFAULT_DURATION;
		if(stream >> duration)
		{
			// a negative/zero duration would make the transition instant; keep it
			// legal but non-negative
			spec.duration = duration > 0.0f ? duration : 0.0f;
		}
		else
		{
			spec.duration = UI_TRANSITION_DEFAULT_DURATION;
		}
		return spec;
	}
	//---------------------------------------------------------
	const char* transitionTypeName(UiTransitionType type)
	{
		switch(type)
		{
		case UTT_Fade:			return "fade";
		case UTT_SlideUp:		return "slide-up";
		case UTT_SlideDown:		return "slide-down";
		case UTT_SlideLeft:		return "slide-left";
		case UTT_SlideRight:	return "slide-right";
		case UTT_Pop:			return "pop";
		case UTT_None:
		default:				return "none";
		}
	}
	//---------------------------------------------------------
	UiTransitionPlan planTransition(UiTransitionSpec const & spec, bool entering,
		float slideDistanceX, float slideDistanceY)
	{
		UiTransitionPlan plan;
		plan.duration = spec.duration;
		if(spec.type == UTT_None)
		{
			return plan;
		}

		// the "away" state each family animates between and the widget's rest
		// (alpha 1, scale 1, offset 0). Entering goes away->rest, exiting rest->away.
		float awayAlpha = 1.0f, awayScale = 1.0f, awayOffX = 0.0f, awayOffY = 0.0f;
		const char* enterEase = "quadOut";
		const char* exitEase = "quadIn";
		switch(spec.type)
		{
		case UTT_Fade:
			plan.animatesAlpha = true;
			awayAlpha = 0.0f;
			break;
		case UTT_SlideUp:
			plan.animatesOffset = true;
			awayOffY = slideDistanceY;		// starts below, slides up to rest
			break;
		case UTT_SlideDown:
			plan.animatesOffset = true;
			awayOffY = -slideDistanceY;		// starts above, slides down to rest
			break;
		case UTT_SlideLeft:
			plan.animatesOffset = true;
			awayOffX = slideDistanceX;		// starts right, slides left to rest
			break;
		case UTT_SlideRight:
			plan.animatesOffset = true;
			awayOffX = -slideDistanceX;		// starts left, slides right to rest
			break;
		case UTT_Pop:
			plan.animatesScale = true;
			awayScale = 0.0f;
			// the pop overshoots into place on enter for the springy feel
			enterEase = "backOut";
			exitEase = "backIn";
			break;
		case UTT_None:
		default:
			break;
		}

		if(entering)
		{
			plan.alphaFrom = awayAlpha;		plan.alphaTo = 1.0f;
			plan.scaleFrom = awayScale;		plan.scaleTo = 1.0f;
			plan.offsetFromX = awayOffX;	plan.offsetToX = 0.0f;
			plan.offsetFromY = awayOffY;	plan.offsetToY = 0.0f;
			plan.ease = enterEase;
		}
		else
		{
			plan.alphaFrom = 1.0f;			plan.alphaTo = awayAlpha;
			plan.scaleFrom = 1.0f;			plan.scaleTo = awayScale;
			plan.offsetFromX = 0.0f;		plan.offsetToX = awayOffX;
			plan.offsetFromY = 0.0f;		plan.offsetToY = awayOffY;
			plan.ease = exitEase;
		}
		return plan;
	}
}
