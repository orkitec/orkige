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
	//! @brief the sky VISUAL an enabled atmosphere draws - the type dialect of
	//! AtmosphereDesc::skyType (pure, both flavors read the same words).
	//!
	//! The type selects ONLY what fills the sky pixels; fog and the
	//! sun/ambient exposure drive ride the enabled atmosphere IDENTICALLY
	//! across all three types (@see AtmosphereDesc).
	namespace AtmosphereSky
	{
		enum Type
		{
			ST_PROCEDURAL = 0,	//!< the model-driven sky dome (the default look)
			ST_SKYBOX,			//!< a cubemap texture (AtmosphereDesc::skyboxTexture)
			ST_COLOUR			//!< flat clear in the sky tint (no dome, no cubemap)
		};

		//! @brief the canonical word for a sky type (the Lua/cvar dialect)
		inline char const * typeName(Type type)
		{
			switch(type)
			{
			case ST_SKYBOX:		return "skybox";
			case ST_COLOUR:		return "colour";
			case ST_PROCEDURAL:
			default:			return "procedural";
			}
		}

		//! @brief parse a type word ("procedural"/"skybox"/"colour",
		//! case-insensitive; "color" is accepted) back to its enum
		//! @return false (outType untouched) on anything else
		inline bool parseType(String const & text, Type & outType)
		{
			String lowered;
			lowered.reserve(text.size());
			for(char each : text)
			{
				lowered.push_back(each >= 'A' && each <= 'Z'
					? static_cast<char>(each - 'A' + 'a') : each);
			}
			if(lowered == "procedural")		{ outType = ST_PROCEDURAL; return true; }
			else if(lowered == "skybox")	{ outType = ST_SKYBOX;     return true; }
			else if(lowered == "colour" ||
				lowered == "color")			{ outType = ST_COLOUR;     return true; }
			return false;
		}
	}

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
	//! the classic compatibility flavor a vertex-colour gradient sky dome reads
	//! @c sky* / @c skyPower / @c density for its zenith->horizon gradient and
	//! the sun glow direction from the first directional light, while @c
	//! fogDensity / @c fog* drive fixed-function exponential fog. @c sunPower /
	//! @c ambientPower drive the sun-exposure linkage on BOTH flavors: next
	//! natively, classic through the same day/night curve evaluated on the CPU
	//! (core_util/AtmosphereSunDrive.h - the linked sun's colour plus an
	//! averaged-flat ambient fill, exposure-calibrated at the mid-grey
	//! reference; a tolerance parity, not per-pixel). While the atmosphere is
	//! enabled it OWNS the linked light's colour; disabling restores the
	//! authored values exactly (@see the capability matrix in
	//! Docs/render-abstraction.md).
	//!
	//! Sky TYPE (@c skyType, @see AtmosphereSky): the type selects what fills
	//! the sky pixels while the atmosphere is enabled - the procedural dome
	//! (the default; everything above applies verbatim), a @c skyboxTexture
	//! cubemap (classic renders it through the native camera-bound sky box,
	//! next samples the same cubemap on the sky quad - both flavors, same
	//! picture), or the flat sky-tint clear alone. Fog and the sun/ambient
	//! exposure drive are sky-type-INDEPENDENT: in skybox/colour mode the
	//! linked sun still follows the SAME elevation-based day/night curve the
	//! procedural sky drives (the @c sky* tint and @c density keep
	//! parameterising that curve plus the window clear colour), scaled by
	//! @c sunPower / @c ambientPower - the cubemap is authored content, so
	//! the sky PICTURE no longer tracks the sun by construction (author the
	//! skybox for the time of day it depicts). A missing/empty
	//! @c skyboxTexture in skybox mode degrades honestly to the flat sky
	//! tint with one log line.
	//!
	//! Field ranges + coupling (next flavor / AtmosphereNpr; verified against
	//! the NprSky model - Components/Atmosphere in the Ogre-Next source):
	//!  - @c skyRed/Green/Blue : the sky's Rayleigh tint. It is NOT a literal
	//!    background colour: the shader mixes it with the sun-height weight and
	//!    the Rayleigh absorption, so a blue-dominant tint reads blue at zenith
	//!    and warms toward the horizon. Keep components in [0;1].
	//!  - @c skyPower : HDR sky-dome brightness multiplier (NprSky finalMultiplier
	//!    scales by it). ~1 = neutral midday; lower for dusk/night. Only touches
	//!    the SKY pixels, not lit surfaces.
	//!  - @c density : Rayleigh density coefficient (NprSky densityCoeff, default
	//!    0.47). Sensible ~[0.05;1.0]: thin (0.08) = a crisp high-altitude sky,
	//!    thick (0.9) = a hazy, washed-out horizon. Couples with sun height: the
	//!    shader divides by sun elevation, so the SAME density reads far hazier
	//!    at a low (sunset) sun than at noon. Above ~1.2 the horizon whites out.
	//!  - @c fogDensity : per-object exponential distance fog (NprSky fogDensity,
	//!    default 0.0001). VERY sensitive - sensible ~[0;0.01]. 0 = none, ~0.002
	//!    is a gentle depth haze at tens of units, ~0.01 buries mid-distance
	//!    geometry. This is object fog, INDEPENDENT of @c density (which is the
	//!    sky dome only); do not confuse the two.
	//!  - @c sunPower : drives the linked directional light's power (NprSky
	//!    linkedLightPower). The pipeline has NO tonemapper, so this is the
	//!    exposure knob for lit surfaces: at the native default (Math::PI) a
	//!    mid-albedo surface under the zenith sun clips to white. The neutral
	//!    default here lands a mid-grey surface in a believable mid-range with
	//!    headroom below 1.0. Sensible ~[0.2;3.0] (0.2 = a dim moonlit night).
	//!  - @c ambientPower : scales the atmosphere-driven hemisphere ambient
	//!    (NprSky linkedSceneAmbient*Power) - the shadowed-surface fill. 1 = the
	//!    native fill; raise for flatter/softer shadows, lower toward 0 for hard,
	//!    dark shadows. Also un-tonemapped, so it stacks with @c sunPower on
	//!    sun-facing surfaces - keep their sum from over-driving bright albedos.
	struct AtmosphereDesc
	{
		bool	enabled;		//!< master switch: false = plain clear background, no fog

		//--- sky visual type ---
		AtmosphereSky::Type	skyType;	//!< what fills the sky pixels (@see AtmosphereSky)
		String	skyboxTexture;	//!< cubemap resource name for ST_SKYBOX (a single
								//!< cubemap .dds, e.g. from Util/make_sky_assets.py)

		//--- sky look ---
		float	skyRed;			//!< zenith sky tint, linear [0;1]
		float	skyGreen;
		float	skyBlue;
		float	skyPower;		//!< HDR sky-dome brightness multiplier (1 = neutral)
		float	density;		//!< sky Rayleigh density coefficient (thicker = hazier horizon)

		//--- surface lighting drive (un-tonemapped exposure knobs) ---
		float	sunPower;		//!< linked directional light power (exposure); native PI clips
		float	ambientPower;	//!< scales the driven hemisphere ambient fill (1 = native)

		//--- per-object exponential distance fog ---
		float	fogDensity;		//!< 0 = no object fog; larger = objects fade to fog sooner
		float	fogRed;			//!< fog colour, linear [0;1] (classic fixed-function fog)
		float	fogGreen;
		float	fogBlue;

		//! neutral defaults: a clear daytime sky, no fog, atmosphere OFF (an
		//! app opts in by setting enabled). density/skyPower mirror the
		//! AtmosphereNpr midday defaults; sunPower/ambientPower are the
		//! un-tonemapped-safe exposure (the native PI sun-power clips) so an
		//! enabled-only desc lights surfaces without blowing out to white.
		AtmosphereDesc()
			: enabled(false)
			, skyType(AtmosphereSky::ST_PROCEDURAL)
			, skyRed(0.334f), skyGreen(0.57f), skyBlue(1.0f)
			, skyPower(1.0f)
			, density(0.47f)
			, sunPower(1.6f)
			, ambientPower(1.0f)
			, fogDensity(0.0f)
			, fogRed(0.7f), fogGreen(0.8f), fogBlue(0.9f)
		{
		}
	};

	//! @brief named sky "looks" that pre-fill an AtmosphereDesc - the friendly
	//! entry point (a game says "sunset", then tweaks). Pure so it unit-tests
	//! headlessly and both flavors read the same numbers. The looks author the
	//! PROCEDURAL sky: forSky/blend leave skyType/skyboxTexture at their
	//! defaults (the Engine Lua wrappers carry a previously-chosen sky type
	//! across preset calls - @see Engine::setAtmosphereSky).
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
				desc.sunPower = 1.6f;
				desc.ambientPower = 1.0f;
				desc.fogDensity = 0.0f;
				desc.fogRed = 0.75f; desc.fogGreen = 0.85f; desc.fogBlue = 0.95f;
				break;
			case SKY_SUNSET:
				desc.skyRed = 1.0f; desc.skyGreen = 0.5f; desc.skyBlue = 0.25f;
				desc.skyPower = 1.0f;
				desc.density = 0.9f;
				// a warm low sun reads dimmer than noon; a touch of fill keeps
				// the long shadows readable, a whisper of object haze
				desc.sunPower = 1.3f;
				desc.ambientPower = 1.2f;
				desc.fogDensity = 0.0025f;
				desc.fogRed = 0.85f; desc.fogGreen = 0.55f; desc.fogBlue = 0.4f;
				break;
			case SKY_NIGHT:
				desc.skyRed = 0.05f; desc.skyGreen = 0.08f; desc.skyBlue = 0.18f;
				// the sky model amplifies a low sun hard (its light density
				// divides by the sun elevation), so a dark night needs BOTH a
				// low dome brightness and a thin Rayleigh density - at the
				// earlier 0.2/0.08 the rendered night dome washed out to a
				// bright haze once it actually drew
				desc.skyPower = 0.1f;
				desc.density = 0.04f;
				// a dim moon: a low sun drive, a cool ambient fill so shapes
				// stay legible instead of crushing to black
				desc.sunPower = 0.35f;
				desc.ambientPower = 0.8f;
				desc.fogDensity = 0.0015f;
				desc.fogRed = 0.04f; desc.fogGreen = 0.06f; desc.fogBlue = 0.12f;
				break;
			case SKY_CUSTOM:
			default:
				break;
			}
			return desc;
		}

		//! @brief field-wise blend between two named looks, @c t in [0;1]
		//! (t=0 -> @c a, t=1 -> @c b). The result is enabled. The linear lerp
		//! is honest for these hand-tuned looks: every field moves monotonically
		//! day -> sunset -> night, so a mid blend is a plausible in-between sky
		//! (the couplings in AtmosphereDesc are per-field-monotone across the
		//! three presets). Pure, so a day/night director drives it headlessly
		//! and both flavors read the same numbers.
		inline AtmosphereDesc blend(Sky a, Sky b, float t)
		{
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			const AtmosphereDesc da = forSky(a);
			const AtmosphereDesc db = forSky(b);
			auto mix = [t](float x, float y) { return x + (y - x) * t; };
			AtmosphereDesc out;
			out.enabled = true;
			out.skyRed = mix(da.skyRed, db.skyRed);
			out.skyGreen = mix(da.skyGreen, db.skyGreen);
			out.skyBlue = mix(da.skyBlue, db.skyBlue);
			out.skyPower = mix(da.skyPower, db.skyPower);
			out.density = mix(da.density, db.density);
			out.sunPower = mix(da.sunPower, db.sunPower);
			out.ambientPower = mix(da.ambientPower, db.ambientPower);
			out.fogDensity = mix(da.fogDensity, db.fogDensity);
			out.fogRed = mix(da.fogRed, db.fogRed);
			out.fogGreen = mix(da.fogGreen, db.fogGreen);
			out.fogBlue = mix(da.fogBlue, db.fogBlue);
			return out;
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
