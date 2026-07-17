/**************************************************************
	created:	2026/07/12 at 16:00
	filename: 	ShadowPreset.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ShadowPreset_h__12_7_2026__16_00_00__
#define __ShadowPreset_h__12_7_2026__16_00_00__

#include "core_util/String.h"

namespace Orkige
{
	//! @brief pure shadow-mapping quality policy: the one place that maps the
	//! coarse quality knob (off/low/medium/high - the `r.shadowQuality` cvar and
	//! `RenderWorld::setShadowQuality`) to concrete budgets: cascade (PSSM
	//! split) count, shadow-map resolutions, filter tap width and the shadow
	//! draw distance. The presets are deliberately phone-sized: MEDIUM (the
	//! default) is what a mobile GPU comfortably renders - 2 cascades on a
	//! 1024x1536 depth atlas (~6 MB) with a 3x3 filter; HIGH is the desktop
	//! step-up; LOW is the constrained-GPU (GLES2/WebGL) budget - ONE split
	//! (a single focused map, no cascades), a 1024 16-bit map and a hard-
	//! clamped shadow distance (near-world shadows only - distance is the
	//! cheapest quality lever, it halves the world each texel must cover).
	//! Renderer-independent so it unit tests headlessly and both render
	//! flavors read the same numbers.
	//! @remarks Atlas layout convention (splitAtlasOffset/atlasSize): the first
	//! cascade gets the full base resolution, every further cascade half of it
	//! (distant cascades cover more world per texel anyway), stacked below the
	//! first left-aligned in one atlas texture.
	//! @remarks filterTaps is a REQUEST the backend rounds to its nearest
	//! filter: 1 means "the cheapest sampling the backend offers" (a 2x2
	//! hardware-compare fetch on both flavors - neither exposes a true single
	//! tap), 3 a 3x3 kernel, 4 a 4x4/16-tap kernel.
	//! @remarks splitCount 1 is NOT rendered as a one-split PSSM: both
	//! backends collapse it to their plain single focused shadow map (the
	//! Ogre-Next compositor requires PSSM splits >= 2 and a 1-split PSSM is
	//! that same focused map by construction).
	namespace ShadowPreset
	{
		//! the coarse quality knob (the canonical order - budgets grow with it)
		enum Quality
		{
			SQ_OFF = 0,		//!< no dynamic shadows rendered
			SQ_LOW,			//!< GLES2/web floor: 1 focused map, 1024, cheapest filter, short reach
			SQ_MEDIUM,		//!< the DEFAULT (phone budget): 2 cascades, 1024 base, 3x3 filter
			SQ_HIGH			//!< desktop: 3 cascades, 2048 base, 4x4 filter
		};

		//! @brief the concrete budgets behind one quality step
		struct Settings
		{
			int				splitCount;		//!< PSSM cascade count (0 = shadows off)
			unsigned int	baseResolution;	//!< first cascade's shadow map edge, texels
			int				filterTaps;		//!< PCF filter width N (an NxN tap kernel)
			float			maxDistance;	//!< world-unit distance shadows reach
		};

		//! @brief budgets for a quality step (SQ_OFF = all zero)
		inline Settings forQuality(Quality quality)
		{
			Settings settings = { 0, 0u, 0, 0.0f };
			switch(quality)
			{
			case SQ_LOW:
				settings.splitCount = 1;
				settings.baseResolution = 1024u;
				settings.filterTaps = 1;
				settings.maxDistance = 30.0f;
				break;
			case SQ_MEDIUM:
				settings.splitCount = 2;
				settings.baseResolution = 1024u;
				settings.filterTaps = 3;
				settings.maxDistance = 60.0f;
				break;
			case SQ_HIGH:
				settings.splitCount = 3;
				settings.baseResolution = 2048u;
				settings.filterTaps = 4;
				settings.maxDistance = 120.0f;
				break;
			case SQ_OFF:
			default:
				break;
			}
			return settings;
		}

		//! @brief one cascade's shadow-map edge length: the first cascade gets
		//! the full base resolution, every further cascade half of it
		inline unsigned int splitResolution(Settings const & settings, int split)
		{
			if(split <= 0)
			{
				return settings.baseResolution;
			}
			return settings.baseResolution / 2u;
		}

		//! @brief one cascade's top-left placement inside the shared atlas
		//! texture: cascade 0 at the origin, the half-size later cascades in a
		//! row directly below it
		inline void splitAtlasOffset(Settings const & settings, int split,
			unsigned int & outX, unsigned int & outY)
		{
			if(split <= 0)
			{
				outX = 0u;
				outY = 0u;
				return;
			}
			outX = (static_cast<unsigned int>(split) - 1u) *
				(settings.baseResolution / 2u);
			outY = settings.baseResolution;
		}

		//! @brief the total atlas texture size the layout above needs
		inline void atlasSize(Settings const & settings,
			unsigned int & outWidth, unsigned int & outHeight)
		{
			if(settings.splitCount <= 0 || settings.baseResolution == 0u)
			{
				outWidth = 0u;
				outHeight = 0u;
				return;
			}
			outWidth = settings.baseResolution;
			outHeight = settings.splitCount > 1
				? settings.baseResolution + settings.baseResolution / 2u
				: settings.baseResolution;
		}

		//! @brief the canonical knob word for a quality step (the cvar dialect)
		inline char const * qualityName(Quality quality)
		{
			switch(quality)
			{
			case SQ_LOW:	return "low";
			case SQ_MEDIUM:	return "medium";
			case SQ_HIGH:	return "high";
			case SQ_OFF:
			default:		return "off";
			}
		}

		//! @brief parse a knob word ("off"/"low"/"medium"/"high",
		//! case-insensitive) back to its quality step
		//! @return false (outQuality untouched) on anything else
		inline bool parseQuality(String const & text, Quality & outQuality)
		{
			String lowered;
			lowered.reserve(text.size());
			for(char each : text)
			{
				lowered.push_back(each >= 'A' && each <= 'Z'
					? static_cast<char>(each - 'A' + 'a') : each);
			}
			if(lowered == "off")			{ outQuality = SQ_OFF;    return true; }
			else if(lowered == "low")		{ outQuality = SQ_LOW;    return true; }
			else if(lowered == "medium")	{ outQuality = SQ_MEDIUM; return true; }
			else if(lowered == "high")		{ outQuality = SQ_HIGH;   return true; }
			return false;
		}
	}
}

#endif //__ShadowPreset_h__12_7_2026__16_00_00__
