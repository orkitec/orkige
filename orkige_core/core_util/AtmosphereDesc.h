/**************************************************************
	created:	2026/07/12 at 20:00
	filename: 	AtmosphereDesc.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __AtmosphereDesc_h__12_7_2026__20_00_00__
#define __AtmosphereDesc_h__12_7_2026__20_00_00__

#include "core_util/String.h"

namespace Orkige
{
	//! @brief pure, renderer-independent description of a scene's sky/fog
	//! atmosphere - what RenderWorld::setAtmosphere consumes.
	//!
	//! Deliberately minimal + honest: a master switch, a sky tint + a couple of
	//! look scalars, and one exponential per-object fog knob. Colours are plain
	//! linear [0;1] floats (this header sits below the render math layer, so it
	//! carries no Color type) - the backends map them to their native colour
	//! type. The SUN is NOT in here: the atmosphere links to the FIRST
	//! directional light in the world and reads its direction as the sun's -
	//! author/animate the day/night arc by orienting that light
	//! (@see RenderWorld::setAtmosphere).
	//!
	//! Flavor honesty: on the Ogre-Next flavor every field drives the native
	//! AtmosphereNpr (sky dome + HlmsPbs-integrated fog + sun colour/power). On
	//! the classic compatibility flavor there is no sky dome: @c fogDensity /
	//! @c fog* drive fixed-function exponential fog and @c sky* becomes the flat
	//! window clear colour (@c skyPower / @c density are ignored) - the honest
	//! subset (@see the capability matrix in Docs/render-abstraction.md).
	struct AtmosphereDesc
	{
		bool	enabled;		//!< master switch: false = plain clear background, no fog

		//--- sky look ---
		float	skyRed;			//!< zenith sky tint, linear [0;1]
		float	skyGreen;
		float	skyBlue;
		float	skyPower;		//!< HDR sky brightness multiplier (1 = neutral)
		float	density;		//!< atmospheric haze density coefficient (thicker = hazier horizon)

		//--- per-object exponential distance fog ---
		float	fogDensity;		//!< 0 = no object fog; larger = objects fade to fog sooner
		float	fogRed;			//!< fog colour, linear [0;1] (classic fixed-function fog)
		float	fogGreen;
		float	fogBlue;

		//! neutral defaults: a clear daytime sky, no fog, atmosphere OFF (an
		//! app opts in by setting enabled). density/skyPower mirror the
		//! AtmosphereNpr midday defaults so an enabled-only desc looks sane.
		AtmosphereDesc()
			: enabled(false)
			, skyRed(0.334f), skyGreen(0.57f), skyBlue(1.0f)
			, skyPower(1.0f)
			, density(0.47f)
			, fogDensity(0.0f)
			, fogRed(0.7f), fogGreen(0.8f), fogBlue(0.9f)
		{
		}
	};

	//! @brief named sky "looks" that pre-fill an AtmosphereDesc - the friendly
	//! entry point (a game says "sunset", then tweaks). Pure so it unit-tests
	//! headlessly and both flavors read the same numbers.
	namespace AtmospherePreset
	{
		//! the canonical named looks (the cvar/word dialect below)
		enum Sky
		{
			SKY_CUSTOM = 0,	//!< no named look - the desc's own fields stand
			SKY_DAY,		//!< clear blue midday
			SKY_SUNSET,		//!< warm hazy low sun
			SKY_NIGHT		//!< dim blue night
		};

		//! @brief a fully-filled enabled AtmosphereDesc for a named look
		//! (SKY_CUSTOM returns the neutral default with enabled=true)
		inline AtmosphereDesc forSky(Sky sky)
		{
			AtmosphereDesc desc;
			desc.enabled = true;
			switch(sky)
			{
			case SKY_DAY:
				desc.skyRed = 0.334f; desc.skyGreen = 0.57f; desc.skyBlue = 1.0f;
				desc.skyPower = 1.0f;
				desc.density = 0.47f;
				desc.fogDensity = 0.0f;
				desc.fogRed = 0.75f; desc.fogGreen = 0.85f; desc.fogBlue = 0.95f;
				break;
			case SKY_SUNSET:
				desc.skyRed = 1.0f; desc.skyGreen = 0.5f; desc.skyBlue = 0.25f;
				desc.skyPower = 1.0f;
				desc.density = 0.9f;
				desc.fogDensity = 0.0025f;
				desc.fogRed = 0.85f; desc.fogGreen = 0.55f; desc.fogBlue = 0.4f;
				break;
			case SKY_NIGHT:
				desc.skyRed = 0.05f; desc.skyGreen = 0.08f; desc.skyBlue = 0.18f;
				desc.skyPower = 0.2f;
				desc.density = 0.08f;
				desc.fogDensity = 0.0015f;
				desc.fogRed = 0.04f; desc.fogGreen = 0.06f; desc.fogBlue = 0.12f;
				break;
			case SKY_CUSTOM:
			default:
				break;
			}
			return desc;
		}

		//! @brief the canonical word for a named look (the cvar/manifest dialect)
		inline char const * skyName(Sky sky)
		{
			switch(sky)
			{
			case SKY_DAY:		return "day";
			case SKY_SUNSET:	return "sunset";
			case SKY_NIGHT:		return "night";
			case SKY_CUSTOM:
			default:			return "custom";
			}
		}

		//! @brief parse a look word ("custom"/"day"/"sunset"/"night",
		//! case-insensitive) back to its enum
		//! @return false (outSky untouched) on anything else
		inline bool parseSky(String const & text, Sky & outSky)
		{
			String lowered;
			lowered.reserve(text.size());
			for(char each : text)
			{
				lowered.push_back(each >= 'A' && each <= 'Z'
					? static_cast<char>(each - 'A' + 'a') : each);
			}
			if(lowered == "custom")			{ outSky = SKY_CUSTOM; return true; }
			else if(lowered == "day")		{ outSky = SKY_DAY;    return true; }
			else if(lowered == "sunset")	{ outSky = SKY_SUNSET; return true; }
			else if(lowered == "night")		{ outSky = SKY_NIGHT;  return true; }
			return false;
		}
	}
}

#endif //__AtmosphereDesc_h__12_7_2026__20_00_00__
