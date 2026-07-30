/********************************************************************
	created:	Wednesday 2026/07/29 at 10:00
	filename: 	SfxDesc.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SfxDesc_h__29_7_2026__10_00_00__
#define __SfxDesc_h__29_7_2026__10_00_00__

//! @file SfxDesc.h
//! @brief the parameters of ONE procedural sound effect, in the STANDARD
//! retro-sfx parameter set, plus the archetype generators that seed them
//! @remarks A sound effect in this engine can be a small PARAMETER file the
//! engine synthesizes at load instead of a recorded wave file (@see SfxSynth,
//! SfxAsset). The parameter set below is deliberately NOT an invention: it is
//! the sfxr parameter model - the de-facto standard for procedural game sound
//! effects, shared verbatim by every free tool in that family - down to the
//! wave-type numbering and the meaning and range of every field. That is what
//! lets a designer author a sound in ANY of those tools, drop the resulting
//! `.sfs` file into the project, and hear the same effect the tool played.
//!
//! FORMAT PROVENANCE: the parameter model, the `.sfs` file layout and the
//! archetype generators are an external, publicly documented specification.
//! This engine implements them from that documentation - the synthesis
//! algorithm, the binary reader and the generators are all written here, with
//! no code taken from any implementation of them (their licences differ and
//! some are copyleft). @see Docs/sound.md for the full provenance note and the
//! field-by-field mapping to the standard's own parameter names.

#include "core_util/String.h"
#include "core_util/StringUtil.h"

namespace Orkige
{
	/** \addtogroup Util
	*  @{ */

	//! @brief the oscillator shapes of the standard parameter set
	//! @remarks The NUMBERING is part of the standard (it is what a `.sfs`
	//! file stores), so it must not be reordered: 0 square, 1 saw, 2 sine,
	//! 3 noise.
	namespace SfxWave
	{
		enum Type
		{
			SW_SQUARE = 0,	//!< pulse wave with a duty cycle and duty sweep
			SW_SAW,			//!< bright, harsh: lasers, engines
			SW_SINE,		//!< pure tone: soft blips, bells
			SW_NOISE		//!< white noise: explosions, hits
		};

		//! the directive spelling of a wave (the parser's inverse of parseType)
		inline char const * typeName(Type type)
		{
			switch(type)
			{
			case SW_SAW:	return "saw";
			case SW_SINE:	return "sine";
			case SW_NOISE:	return "noise";
			case SW_SQUARE:
			default:		return "square";
			}
		}

		//! @brief map a `wave` directive's value (case-insensitive) to its enum
		//! @return false for an unknown name, @p outType untouched
		inline bool parseType(String const & text, Type & outType)
		{
			const String key = StringUtil::to_lower_copy(text);
			if(key == "square")		{ outType = SW_SQUARE;	return true; }
			if(key == "saw")		{ outType = SW_SAW;		return true; }
			if(key == "sine")		{ outType = SW_SINE;	return true; }
			if(key == "noise")		{ outType = SW_NOISE;	return true; }
			return false;
		}

		//! @brief map a stored wave NUMBER (the `.sfs` field) to its enum
		//! @return false when the number names no wave, @p outType untouched
		inline bool fromNumber(int number, Type & outType)
		{
			if(number < SW_SQUARE || number > SW_NOISE)
			{
				return false;
			}
			outType = static_cast<Type>(number);
			return true;
		}
	}

	//! @brief the complete description of one procedural sound effect: the
	//! standard parameter set, one field per standard parameter
	//! @remarks Field names are this codebase's spelling of the standard's own
	//! `p_*` parameter names (the mapping table lives in Docs/sound.md).
	//! RANGES are the standard's: every field is a normalized 0..1 weight
	//! except the SIGNED ones (the slides, sweeps, the arpeggio amount and the
	//! delay tap), which run -1..1. They are weights, not physical units - the
	//! synthesizer turns them into periods, sample counts and filter
	//! coefficients (@see SfxSynth). Defaults are the standard's reset state: a
	//! plain square blip.
	struct SfxDesc
	{
		//--- oscillator ---
		SfxWave::Type	waveType = SfxWave::SW_SQUARE;	//!< the shape (@see SfxWave)

		//--- volume ---
		float	soundVolume = 0.5f;		//!< this effect's own volume 0..1
		//! @brief the output gain applied on top of soundVolume, 0..1
		//! @remarks The standard's app-level output slider. Defaults to 1 here
		//! (the convention every embedded implementation of the model uses) so
		//! a stock effect is audible; the authoring tools' own default is a
		//! much lower monitoring level, which is why an imported `.sfs`
		//! deliberately does NOT carry this field.
		float	masterVolume = 1.0f;

		//--- frequency ---
		float	baseFreq = 0.3f;		//!< start frequency 0..1
		float	freqLimit = 0.0f;		//!< the frequency a downward slide stops at, 0..1 (0 = none)
		float	freqRamp = 0.0f;		//!< slide, -1..1 (positive rises)
		float	freqDeltaRamp = 0.0f;	//!< delta slide (the slide's own slide), -1..1

		//--- square duty (square wave only) ---
		float	duty = 0.0f;			//!< pulse width 0..1
		float	dutyRamp = 0.0f;		//!< duty sweep, -1..1

		//--- vibrato ---
		float	vibratoStrength = 0.0f;	//!< depth 0..1
		float	vibratoSpeed = 0.0f;	//!< speed 0..1
		//! @brief vibrato delay 0..1 - carried for `.sfs` round-trip fidelity
		//! @remarks The standard STORES this parameter but its reference
		//! synthesis never reads it, so neither does ours: honoring it would
		//! make our output differ from every tool in the family.
		float	vibratoDelay = 0.0f;

		//--- envelope (weights, not seconds - @see SfxSynth) ---
		float	attack = 0.0f;			//!< attack time 0..1
		float	sustain = 0.3f;			//!< sustain time 0..1
		float	decay = 0.4f;			//!< decay time 0..1
		float	punch = 0.0f;			//!< sustain punch 0..1 (an amplitude spike at the sustain's start)

		//--- filters ---
		//! @brief the authoring tools' "filter enabled" checkbox, 0..1
		//! @remarks Carried for `.sfs` round-trip fidelity only: the standard's
		//! synthesis gates its low-pass on `lpfFreq != 1`, never on this flag.
		bool	filterOn = false;
		float	lpfResonance = 0.0f;	//!< low-pass resonance 0..1
		float	lpfFreq = 1.0f;			//!< low-pass cutoff 0..1 (1 = filter off)
		float	lpfRamp = 0.0f;			//!< low-pass cutoff sweep, -1..1
		float	hpfFreq = 0.0f;			//!< high-pass cutoff 0..1 (0 = off)
		float	hpfRamp = 0.0f;			//!< high-pass cutoff sweep, -1..1

		//--- the swept delay tap ---
		float	phaserOffset = 0.0f;	//!< delay-tap offset, -1..1
		float	phaserRamp = 0.0f;		//!< delay-tap sweep, -1..1

		//--- retrigger / arpeggio ---
		float	repeatSpeed = 0.0f;		//!< retrigger rate 0..1 (0 = no repeat)
		float	arpSpeed = 0.0f;		//!< arpeggio change speed 0..1 (1 = off)
		float	arpMod = 0.0f;			//!< arpeggio change amount, -1..1

		//--- rendering (NOT part of the standard's parameter set) ---
		//! @brief the seed of the noise oscillator and of an archetype
		//! generator, so a stored effect renders byte-identically every time
		//! @remarks The authoring tools draw their noise from a global,
		//! unseeded generator - fine for a tool, useless for an ASSET, which
		//! must sound the same on every load and in every test. This field is
		//! ours, defaults to a fixed value and is absent from `.sfs` (an
		//! imported file simply gets the default).
		unsigned int	seed = 1u;
	};

	//! @brief the archetype generators: the standard's own "make me a sound of
	//! this kind" recipes
	//! @remarks Each archetype is a documented recipe of ranges rather than
	//! one fixed sound - a coin pickup is a FAMILY. Drawing from a SEED makes
	//! a specific member reproducible: `forArchetype(SA_PICKUP_COIN)`
	//! always yields the same coin, and a different seed yields a different
	//! one, which is exactly the "generate again until it sounds right" loop
	//! the authoring tools offer. A preset SEEDS every field of an SfxDesc and
	//! the text asset's explicit directives then override individual fields;
	//! that precedence is enforced structurally (@see SfxAsset::parse).
	namespace SfxPreset
	{
		//! the standard generator set (the `preset` directive's vocabulary)
		enum Archetype
		{
			SA_PICKUP_COIN = 0,	//!< a collected pickup
			SA_LASER_SHOOT,		//!< a shot
			SA_EXPLOSION,		//!< an explosion
			SA_POWERUP,			//!< an upgrade / level-up
			SA_HIT_HURT,		//!< an impact / taking damage
			SA_JUMP,			//!< a jump
			SA_BLIP_SELECT		//!< a UI selection
		};

		//! the canonical directive spelling of an archetype
		inline char const * archetypeName(Archetype archetype)
		{
			switch(archetype)
			{
			case SA_LASER_SHOOT:	return "laser";
			case SA_EXPLOSION:		return "explosion";
			case SA_POWERUP:		return "powerup";
			case SA_HIT_HURT:		return "hit";
			case SA_JUMP:			return "jump";
			case SA_BLIP_SELECT:	return "blip";
			case SA_PICKUP_COIN:
			default:				return "coin";
			}
		}

		//! @brief map a `preset` directive's value to its archetype.
		//! Case-insensitive, and the standard's own alternative names for the
		//! same generator ("pickup", "shoot", "hurt", "select") resolve to the
		//! same recipe.
		//! @return false for an unknown name, @p outArchetype untouched
		inline bool parseArchetype(String const & text, Archetype & outArchetype)
		{
			const String key = StringUtil::to_lower_copy(text);
			if(key == "coin" || key == "pickup")
			{
				outArchetype = SA_PICKUP_COIN;		return true;
			}
			if(key == "laser" || key == "shoot")
			{
				outArchetype = SA_LASER_SHOOT;		return true;
			}
			if(key == "explosion")
			{
				outArchetype = SA_EXPLOSION;		return true;
			}
			if(key == "powerup")
			{
				outArchetype = SA_POWERUP;			return true;
			}
			if(key == "hit" || key == "hurt")
			{
				outArchetype = SA_HIT_HURT;			return true;
			}
			if(key == "jump")
			{
				outArchetype = SA_JUMP;				return true;
			}
			if(key == "blip" || key == "select")
			{
				outArchetype = SA_BLIP_SELECT;		return true;
			}
			return false;
		}

		//! the number of generators (SA_PICKUP_COIN .. SA_BLIP_SELECT)
		const int ARCHETYPE_COUNT = static_cast<int>(SA_BLIP_SELECT) + 1;

		//! @brief draw one sound of @p archetype's family, reproducibly.
		//! @param seed picks the family member; the SAME seed always yields
		//! the SAME parameters (and is carried into the result's SfxDesc::seed
		//! so its noise is reproducible too)
		//! @remarks Defined in SfxSynth.cpp (the generators share the
		//! synthesizer's deterministic random source).
		SfxDesc forArchetype(Archetype archetype, unsigned int seed = 1u);
	}

	/** @} */
}

#endif //__SfxDesc_h__29_7_2026__10_00_00__
