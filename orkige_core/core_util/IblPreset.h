/**************************************************************
	created:	2026/07/18 at 09:00
	filename: 	IblPreset.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __IblPreset_h__18_7_2026__09_00_00__
#define __IblPreset_h__18_7_2026__09_00_00__

#include "core_util/String.h"

namespace Orkige
{
	//! @brief pure image-based-lighting quality policy: the one place that maps
	//! the coarse quality knob (off/low/medium/high - the `r.iblQuality` cvar
	//! and `RenderWorld::setIblQuality`) to the concrete budget: the resolution
	//! of the prefiltered environment chain both render flavors sample for
	//! cubemap-sourced specular reflections + diffuse fill. The source is the
	//! scene's skybox cubemap (AtmosphereDesc::skyboxTexture - its mip chain IS
	//! the roughness chain, prefiltered offline by Util/make_sky_assets.py);
	//! a tier below the source's edge drops leading mips (the whole-chain
	//! subset, no re-filtering), so a tier change is a cheap re-upload.
	//! Renderer-independent so it unit tests headlessly and both render flavors
	//! read the same numbers.
	//! @remarks The presets are deliberately phone-sized: MEDIUM (the default
	//! knob position) keeps a 64-texel chain (~130 KB of cubemap) - plenty for
	//! the engine's stylized skies; HIGH is the desktop step-up that takes a
	//! detailed skybox verbatim; LOW is the constrained-GPU floor.
	//! @remarks The knob position alone renders nothing: image lighting also
	//! needs the opt-in (`RenderWorld::setImageLighting`) AND a loaded skybox
	//! cubemap - until all three hold, the scene pays neither memory nor
	//! per-frame cost and renders byte-identically.
	namespace IblPreset
	{
		//! the coarse quality knob (the canonical order - budgets grow with it)
		enum Quality
		{
			IQ_OFF = 0,		//!< no image-based lighting rendered
			IQ_LOW,			//!< constrained-GPU floor: a 32-texel chain
			IQ_MEDIUM,		//!< the DEFAULT (phone budget): a 64-texel chain
			IQ_HIGH			//!< desktop: up to a 256-texel chain
		};

		//! @brief the concrete budget behind one quality step
		struct Settings
		{
			//! the prefiltered environment chain's top-mip edge cap, texels
			//! (0 = image lighting off)
			unsigned int	chainResolution;
		};

		//! @brief the budget for a quality step (IQ_OFF = zero)
		inline Settings forQuality(Quality quality)
		{
			Settings settings = { 0u };
			switch(quality)
			{
			case IQ_LOW:	settings.chainResolution = 32u;		break;
			case IQ_MEDIUM:	settings.chainResolution = 64u;		break;
			case IQ_HIGH:	settings.chainResolution = 256u;	break;
			case IQ_OFF:
			default:		break;
			}
			return settings;
		}

		//! @brief how many leading mips of a source cubemap to drop so its
		//! remaining chain fits the tier's resolution cap. A source already
		//! within the cap (or a zero cap/edge) skips nothing; the skip never
		//! consumes the whole chain (at least the 1-texel tail remains).
		inline unsigned int mipSkipForSource(unsigned int sourceEdge,
			Settings const & settings)
		{
			if(settings.chainResolution == 0u || sourceEdge == 0u)
			{
				return 0u;
			}
			unsigned int skip = 0u;
			while(sourceEdge > settings.chainResolution && sourceEdge > 1u)
			{
				sourceEdge /= 2u;
				++skip;
			}
			return skip;
		}

		//! @brief the multiplier the authored image-lighting intensity is scaled
		//! by before it reaches the shaders - the ONE number BOTH render flavors
		//! consume (next: the HlmsPbs envmapScale lane the ambient write carries;
		//! classic: the image-lighting stage's luminance AND the water mirror's
		//! energy weight), so the same authored intensity lights the same scene
		//! identically on both flavors by construction.
		//! @remarks The value is a LOOK choice, not a derived constant: the
		//! environment fill ADDS on top of the driven hemisphere ambient (which
		//! already carries the sky's diffuse energy), so full-strength sky
		//! radiance would double-count that ambient - this weight dials the
		//! composed level. Cross-flavor parity does NOT depend on the value:
		//! both flavors run the same env-term formula, so retuning the look is
		//! a one-constant change that keeps the flavors matched.
		inline float fillScale() { return 0.2f; }

		//! @brief the canonical knob word for a quality step (the cvar dialect)
		inline char const * qualityName(Quality quality)
		{
			switch(quality)
			{
			case IQ_LOW:	return "low";
			case IQ_MEDIUM:	return "medium";
			case IQ_HIGH:	return "high";
			case IQ_OFF:
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
			if(lowered == "off")			{ outQuality = IQ_OFF;    return true; }
			else if(lowered == "low")		{ outQuality = IQ_LOW;    return true; }
			else if(lowered == "medium")	{ outQuality = IQ_MEDIUM; return true; }
			else if(lowered == "high")		{ outQuality = IQ_HIGH;   return true; }
			return false;
		}
	}
}

#endif //__IblPreset_h__18_7_2026__09_00_00__
