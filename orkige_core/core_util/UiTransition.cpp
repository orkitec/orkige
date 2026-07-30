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

#include <algorithm>
#include <sstream>

namespace Orkige
{
	const float UI_TRANSITION_DEFAULT_DURATION = 0.2f;

	namespace
	{
		//! the family word of a clause, normalised: lower case with underscores
		//! folded onto hyphens ("SLIDE_UP" -> "slide-up")
		String normaliseFamily(String const & word)
		{
			String family = word;
			StringUtil::to_lower(family);
			for(char & c : family)
			{
				if(c == '_')
				{
					c = '-';
				}
			}
			return family;
		}
		//! resolve a family word to its type; UTT_None for "none" AND for an
		//! unknown word (the caller tells them apart by comparing the word)
		UiTransitionType familyType(String const & family)
		{
			if(family == "fade")		return UTT_Fade;
			if(family == "slide-up")	return UTT_SlideUp;
			if(family == "slide-down")	return UTT_SlideDown;
			if(family == "slide-left")	return UTT_SlideLeft;
			if(family == "slide-right")	return UTT_SlideRight;
			if(family == "slide")		return UTT_Slide;
			if(family == "pop")			return UTT_Pop;
			return UTT_None;
		}
		//! is @p token a plain decimal number (the duration / offset tokens)?
		bool parseNumber(String const & token, float & out)
		{
			std::istringstream stream(token);
			float value = 0.0f;
			if(!(stream >> value))
			{
				return false;
			}
			String tail;
			if(stream >> tail)
			{
				return false;	// "40px" and friends are not numbers
			}
			out = value;
			return true;
		}
		//! @brief parse ONE clause ("fade 0.25 quadOut", "slide -40 0"); false
		//! when the clause is malformed (@p why then carries the reason) or is an
		//! explicit "none"
		bool parseClause(String const & text, UiTransitionClause & out, String & why)
		{
			std::istringstream stream(text);
			String word;
			if(!(stream >> word))
			{
				return false;	// an empty clause (a trailing '|') is silently none
			}
			const String family = normaliseFamily(word);
			const UiTransitionType type = familyType(family);
			if(type == UTT_None)
			{
				if(family != "none")
				{
					why = "unknown transition family '" + word + "'";
				}
				return false;
			}
			// the tokens after the family: every NUMBER in order, then at most one
			// non-numeric token, which is the easing curve name
			std::vector<float> numbers;
			String ease;
			String token;
			while(stream >> token)
			{
				float value = 0.0f;
				if(parseNumber(token, value))
				{
					numbers.push_back(value);
				}
				else if(ease.empty())
				{
					ease = token;
				}
				else
				{
					why = "transition clause '" + text + "' carries more than one "
						"easing name ('" + ease + "' and '" + token + "')";
					return false;
				}
			}

			out = UiTransitionClause();
			out.type = type;
			out.ease = ease;
			size_t durationAt = 0;
			if(type == UTT_Slide)
			{
				// an explicit slide needs its away vector before anything else
				if(numbers.size() < 2)
				{
					why = "'slide' needs an explicit offset ('slide dx dy'); use "
						"slide-up / slide-down / slide-left / slide-right for the "
						"widget-sized travel";
					return false;
				}
				out.offsetX = numbers[0];
				out.offsetY = numbers[1];
				durationAt = 2;
			}
			if(numbers.size() > durationAt)
			{
				// a negative/zero duration would make the transition instant; keep
				// it legal but non-negative
				const float duration = numbers[durationAt];
				out.duration = duration > 0.0f ? duration : 0.0f;
			}
			else
			{
				out.duration = UI_TRANSITION_DEFAULT_DURATION;
			}
			if(numbers.size() > durationAt + 1)
			{
				why = "transition clause '" + text + "' carries "
					"more numbers than the family takes - the extra ones are ignored";
				// a surplus number is a diagnostic, not a refusal: the clause it
				// describes is unambiguous, so it still plays
			}
			return true;
		}
	}
	//---------------------------------------------------------
	UiTransitionType UiTransitionSpec::primaryType() const
	{
		return this->clauses.empty() ? UTT_None : this->clauses.front().type;
	}
	//---------------------------------------------------------
	float UiTransitionSpec::totalDuration() const
	{
		float longest = 0.0f;
		for(UiTransitionClause const & clause : this->clauses)
		{
			longest = std::max(longest, clause.duration);
		}
		return longest;
	}
	//---------------------------------------------------------
	UiTransitionSpec parseTransition(String const & text,
		std::vector<String> * diagnosticsOut)
	{
		UiTransitionSpec spec;
		// clauses compose: `fade 0.25 quadOut | slide 0 -40` drives two channels
		size_t begin = 0;
		while(begin <= text.size())
		{
			const size_t bar = text.find('|', begin);
			const size_t end = (bar == String::npos) ? text.size() : bar;
			const String clauseText = text.substr(begin, end - begin);
			UiTransitionClause clause;
			String why;
			if(parseClause(clauseText, clause, why))
			{
				spec.clauses.push_back(clause);
			}
			if(!why.empty() && diagnosticsOut != NULL)
			{
				diagnosticsOut->push_back(why);
			}
			if(bar == String::npos)
			{
				break;
			}
			begin = bar + 1;
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
		case UTT_Slide:			return "slide";
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
		// each clause claims its channel; clauses of different families therefore
		// compose, and two clauses on the same channel are last-wins
		for(UiTransitionClause const & clause : spec.clauses)
		{
			// the "away" state this family animates between and the widget's rest
			// (alpha 1, scale 1, offset 0). Entering goes away->rest, exiting
			// rest->away.
			float awayAlpha = 1.0f, awayScale = 1.0f;
			float awayOffX = 0.0f, awayOffY = 0.0f;
			const char* enterEase = "quadOut";
			const char* exitEase = "quadIn";
			bool alpha = false, scale = false, offset = false;
			switch(clause.type)
			{
			case UTT_Fade:
				alpha = true;
				awayAlpha = 0.0f;
				break;
			case UTT_SlideUp:
				offset = true;
				awayOffY = slideDistanceY;		// starts below, slides up to rest
				break;
			case UTT_SlideDown:
				offset = true;
				awayOffY = -slideDistanceY;		// starts above, slides down to rest
				break;
			case UTT_SlideLeft:
				offset = true;
				awayOffX = slideDistanceX;		// starts right, slides left to rest
				break;
			case UTT_SlideRight:
				offset = true;
				awayOffX = -slideDistanceX;		// starts left, slides right to rest
				break;
			case UTT_Slide:
				offset = true;
				awayOffX = clause.offsetX;		// the authored away vector
				awayOffY = clause.offsetY;
				break;
			case UTT_Pop:
				scale = true;
				awayScale = 0.0f;
				// the pop overshoots into place on enter for the springy feel
				enterEase = "backOut";
				exitEase = "backIn";
				break;
			case UTT_None:
			default:
				continue;
			}
			// an explicit ease wins over the family's own default
			const String ease = clause.ease.empty()
				? String(entering ? enterEase : exitEase)
				: clause.ease;
			if(alpha)
			{
				plan.animatesAlpha = true;
				plan.alphaFrom = entering ? awayAlpha : 1.0f;
				plan.alphaTo = entering ? 1.0f : awayAlpha;
				plan.alphaDuration = clause.duration;
				plan.alphaEase = ease;
			}
			if(scale)
			{
				plan.animatesScale = true;
				plan.scaleFrom = entering ? awayScale : 1.0f;
				plan.scaleTo = entering ? 1.0f : awayScale;
				plan.scaleDuration = clause.duration;
				plan.scaleEase = ease;
			}
			if(offset)
			{
				plan.animatesOffset = true;
				plan.offsetFromX = entering ? awayOffX : 0.0f;
				plan.offsetFromY = entering ? awayOffY : 0.0f;
				plan.offsetToX = entering ? 0.0f : awayOffX;
				plan.offsetToY = entering ? 0.0f : awayOffY;
				plan.offsetDuration = clause.duration;
				plan.offsetEase = ease;
			}
		}
		return plan;
	}
	//---------------------------------------------------------
	float UiTransitionPlan::duration() const
	{
		// derived, never stored: a channel's duration is the one truth
		float longest = 0.0f;
		if(this->animatesAlpha)	{ longest = std::max(longest, this->alphaDuration); }
		if(this->animatesScale)	{ longest = std::max(longest, this->scaleDuration); }
		if(this->animatesOffset){ longest = std::max(longest, this->offsetDuration); }
		return longest;
	}
	//---------------------------------------------------------
	String const & UiTransitionPlan::ease() const
	{
		static const String none;
		if(this->animatesAlpha)		{ return this->alphaEase; }
		if(this->animatesScale)		{ return this->scaleEase; }
		if(this->animatesOffset)	{ return this->offsetEase; }
		return none;
	}
}
