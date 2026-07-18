/**************************************************************
	created:	2026/07/18 at 09:00
	filename: 	BloomPreset.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __BloomPreset_h__18_7_2026__09_00_00__
#define __BloomPreset_h__18_7_2026__09_00_00__

#include "core_util/String.h"

namespace Orkige
{
	//! @brief pure, renderer-independent description of a scene's LDR bloom -
	//! what RenderWorld::setBloom consumes.
	//!
	//! Bloom is per-scene OPT-IN and DEFAULT OFF: content that never enables it
	//! renders pixel-identical to a build with no bloom code (the
	//! toggle-identity discipline). @c enabled is the content switch (the scene
	//! or a script turns it on); the @c r.bloomQuality cvar
	//! (@see BloomPreset::Quality) is the orthogonal cost/quality tier. Bloom
	//! renders only while @c enabled is true AND the quality knob is not
	//! BQ_OFF - either being off means no bloom pass and byte-stable output.
	//!
	//! LDR threshold semantics: this is a LOW-dynamic-range bloom. The scene
	//! renders to a clamped [0;1] target (no HDR, no tonemapper), so the
	//! bright-pass extracts the pixels whose luminance already exceeds @c
	//! threshold in [0;1] - the near-white highlights (emissive whites, a
	//! bright sun disk, a specular hot spot). It cannot bloom "over-bright"
	//! values above 1.0 because none survive the LDR target; keep @c threshold
	//! strictly below 1.0 or nothing blooms. The extracted highlights are
	//! blurred and added back scaled by @c intensity. A future HDR phase would
	//! bright-pass in linear HDR with a threshold above 1.0 (over-exposure)
	//! behind an exposure/tonemap contract - explicitly out of scope here
	//! (@see Docs/render-abstraction.md).
	//!
	//! The 2D tier (sprites / vector shapes / gui) is EXCLUDED by contract:
	//! bloom processes the 3D scene only, so UI whites and flat-colour 2D art
	//! stay crisp. The backends sequence the bloom pass before the 2D+UI
	//! composition (@see RenderWorld::setBloom).
	struct BloomDesc
	{
		bool	enabled;	//!< content switch: false = no bloom, byte-identical output
		float	threshold;	//!< bright-pass luminance cutoff, [0;1) (below 1.0 or nothing blooms)
		float	intensity;	//!< additive scale of the blurred highlights (0 = invisible)

		//! neutral defaults: bloom OFF, a high LDR threshold that catches only
		//! near-white highlights, a unit additive intensity. An app opts in by
		//! setting enabled.
		BloomDesc()
			: enabled(false)
			, threshold(0.75f)
			, intensity(1.0f)
		{
		}

		//! @brief the sanitised copy the backends apply: threshold clamped to
		//! [0;0.999] (1.0 blooms nothing in LDR - the honest ceiling), intensity
		//! clamped non-negative. Pure so it unit-tests headlessly and both
		//! flavors feed the compositor identical numbers.
		BloomDesc sanitised() const
		{
			BloomDesc out = *this;
			if(out.threshold < 0.0f)		{ out.threshold = 0.0f; }
			else if(out.threshold > 0.999f)	{ out.threshold = 0.999f; }
			if(out.intensity < 0.0f)		{ out.intensity = 0.0f; }
			return out;
		}
	};

	//! @brief pure bloom quality policy: the one place that maps the coarse
	//! quality knob (off/low/medium/high - the `r.bloomQuality` cvar and
	//! `RenderWorld::setBloomQuality`) to the concrete blur budget behind the
	//! post-process: the downsample factor of the bloom buffer (larger = cheaper
	//! AND softer) and the number of separable blur iterations. The presets are
	//! phone-sized: MEDIUM (the default) is what a mobile GPU comfortably
	//! renders - a quarter-resolution buffer with two blur passes; HIGH is the
	//! desktop step-up (a half-resolution buffer, three passes); LOW is the
	//! constrained-GPU (GLES2/WebGL) floor - a quarter-resolution buffer with a
	//! single pass. Renderer-independent so it unit tests headlessly and both
	//! render flavors read the same numbers.
	//! @remarks threshold + intensity are NOT here: they are per-scene content
	//! (BloomDesc), authored/animated at runtime, not a coarse cost tier.
	namespace BloomPreset
	{
		//! the coarse quality knob (the canonical order - budgets grow with it)
		enum Quality
		{
			BQ_OFF = 0,		//!< no bloom pass rendered (the byte-stable default state)
			BQ_LOW,			//!< GLES2/web floor: quarter-res buffer, one blur pass
			BQ_MEDIUM,		//!< the DEFAULT (phone budget): quarter-res, two passes
			BQ_HIGH			//!< desktop: half-res buffer, three passes
		};

		//! @brief the concrete blur budget behind one quality step
		struct Settings
		{
			int	downsampleFactor;	//!< bloom buffer edge = window edge / this (>=1; 0 = off)
			int	blurPasses;			//!< separable (horizontal+vertical) blur iterations
		};

		//! @brief blur budget for a quality step (BQ_OFF = all zero)
		inline Settings forQuality(Quality quality)
		{
			Settings settings = { 0, 0 };
			switch(quality)
			{
			case BQ_LOW:
				settings.downsampleFactor = 4;
				settings.blurPasses = 1;
				break;
			case BQ_MEDIUM:
				settings.downsampleFactor = 4;
				settings.blurPasses = 2;
				break;
			case BQ_HIGH:
				settings.downsampleFactor = 2;
				settings.blurPasses = 3;
				break;
			case BQ_OFF:
			default:
				break;
			}
			return settings;
		}

		//! @brief the canonical knob word for a quality step (the cvar dialect)
		inline char const * qualityName(Quality quality)
		{
			switch(quality)
			{
			case BQ_LOW:	return "low";
			case BQ_MEDIUM:	return "medium";
			case BQ_HIGH:	return "high";
			case BQ_OFF:
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
			if(lowered == "off")			{ outQuality = BQ_OFF;    return true; }
			else if(lowered == "low")		{ outQuality = BQ_LOW;    return true; }
			else if(lowered == "medium")	{ outQuality = BQ_MEDIUM; return true; }
			else if(lowered == "high")		{ outQuality = BQ_HIGH;   return true; }
			return false;
		}
	}
}

#endif //__BloomPreset_h__18_7_2026__09_00_00__
