/********************************************************************
	created:	Wednesday 2026/07/29 at 10:20
	filename: 	SfxAsset.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SfxAsset_h__29_7_2026__10_20_00__
#define __SfxAsset_h__29_7_2026__10_20_00__

//! @file SfxAsset.h
//! @brief the two codecs of a procedural sound asset: the standard BINARY
//! parameter file `.sfs` and its agent-authorable TEXT twin `.osfx`
//! @remarks ONE parameter model (SfxDesc, the standard retro-sfx set), TWO
//! files that carry it - never two models:
//!
//!   `.sfs`  the STANDARD's own binary parameter file. A designer dials a
//!           sound in any of the free tools of that family, saves, drops the
//!           file into the project, and the engine plays exactly what the tool
//!           played. Read-only here (the tools own authoring).
//!   `.osfx` the same parameters as line-based TEXT, so an agent can write a
//!           sound with `write_project_file` and a human can diff, review and
//!           hand-tune one. Uses the standard's parameter names, adds an
//!           archetype `preset` seed, and round-trips through serialize().
//!
//! Both are SOUND FILES as far as the rest of the engine is concerned:
//! `SoundComponent::addSound("hit", "assets/hit.osfx")`, the Lua sound surface,
//! the mixer groups, per-play pitch/volume variation and positional audio all
//! work verbatim, because the extension dispatch happens at the ONE place a
//! `.wav` is decoded (SoundUtil::loadSoundData).
//!
//! The `.osfx` grammar (v1), one directive per line, `#` starts a comment.
//! Every value is a normalized weight in the standard's range - 0..1, or -1..1
//! for the SIGNED sweeps (freqRamp, freqDeltaRamp, dutyRamp, lpfRamp, hpfRamp,
//! phaserOffset, phaserRamp, arpMod). @see SfxDesc.h for what each one does.
//!   version 1                 optional; only version 1 is accepted
//!   preset NAME               archetype seed: coin | laser | explosion |
//!                             powerup | hit | jump | blip (the standard's
//!                             alternative names pickup/shoot/hurt/select work)
//!   wave NAME                 square | saw | sine | noise
//!   soundVolume F             this effect's volume
//!   masterVolume F            the output gain on top of it
//!   baseFreq F                start frequency
//!   freqLimit F               where a downward slide stops (0 = nowhere)
//!   freqRamp F                slide
//!   freqDeltaRamp F           the slide's own slide
//!   duty F                    square pulse width
//!   dutyRamp F                duty sweep
//!   vibratoStrength F         vibrato depth
//!   vibratoSpeed F            vibrato speed
//!   vibratoDelay F            carried for file fidelity; the standard's
//!                             synthesis does not read it
//!   attack F                  envelope attack time
//!   sustain F                 envelope sustain time
//!   decay F                   envelope decay time
//!   punch F                   sustain punch
//!   filter 0|1                the tools' filter checkbox (fidelity only)
//!   lpfFreq F                 low-pass cutoff (1 = off)
//!   lpfRamp F                 low-pass sweep
//!   lpfResonance F            low-pass resonance
//!   hpfFreq F                 high-pass cutoff (0 = off)
//!   hpfRamp F                 high-pass sweep
//!   phaserOffset F            the swept delay tap's offset
//!   phaserRamp F              the swept delay tap's sweep
//!   repeatSpeed F             retrigger rate (0 = off)
//!   arpSpeed F                arpeggio change speed (1 = off)
//!   arpMod F                  arpeggio change amount
//!   seed N                    the render seed (ours, not the standard's -
//!                             @see SfxDesc::seed)
//!
//! PRECEDENCE: `preset` SEEDS every field, explicit directives OVERRIDE the
//! seeded ones - and that order is STRUCTURAL, not textual: the parser applies
//! the preset before any other directive no matter where the `preset` line sits
//! in the file (the AtmosphereComponent declaration-order rule, expressed as a
//! two-pass parse). So a file cannot half-apply an archetype depending on how
//! its lines happen to be ordered.
//!
//! MALFORMATION: like `.omat`, an unknown or duplicated directive, a wrong
//! arity or a garbage value is an ERROR reported as "line N: ..." - a typo
//! silently ignored would ship a wrong sound with no trace. On any error @p out
//! is left UNTOUCHED, so a failed hot-reload keeps the sound that was already
//! loaded (the `.oui` reload contract). A value merely OUT OF RANGE is not the
//! parser's business: SfxSynth::sanitize clamps it and names what it clamped.

#include "core_util/SfxDesc.h"

#include <cstddef>

namespace Orkige
{
	/** \addtogroup Util
	*  @{ */

	//! @brief the sound-parameter codecs (pure, headless) - @see the file doc
	class SfxAsset
	{
	public:
		//! the TEXT asset's extension (lower case)
		static char const * const TEXT_EXTENSION;		//!< ".osfx"
		//! the STANDARD binary parameter file's extension (lower case)
		static char const * const BINARY_EXTENSION;		//!< ".sfs"

		//! @brief is this resource name a procedural sound asset in EITHER
		//! form (case-insensitive)? The ONE spelling of the question - the
		//! sound loader, the editor's audition affordance and the asset
		//! classifier all ask it here.
		static bool isSfxName(String const & name);
		//! @brief is this resource name the TEXT form specifically?
		static bool isTextName(String const & name);
		//! @brief is this resource name the STANDARD BINARY form specifically?
		static bool isBinaryName(String const & name);

		//! @brief parse `.osfx` text into a sound description.
		//! @return true on a well-formed effect. On ANY malformation it
		//! returns false, leaves @p out UNTOUCHED and describes the problem in
		//! @p outError ("line N: ...") when one is passed.
		static bool parse(String const & text, SfxDesc & out,
			String * outError = NULL);

		//! @brief parse a STANDARD binary parameter file (`.sfs`).
		//! @param data the file's bytes, @p size its length
		//! @return true on a file this reader understands (versions 100, 101
		//! and 102 of the format). On a bad magic/version, a truncated file or
		//! a wave number the format does not define it returns false, leaves
		//! @p out UNTOUCHED and explains in @p outError.
		//! @remarks The layout is a little-endian version int followed by the
		//! parameter values in a fixed order (@see Docs/sound.md for the field
		//! order and which versions carry which fields). Fields the format does
		//! not carry - our render seed, and the output gain, whose default in
		//! the authoring tools is a low monitoring level - keep their SfxDesc
		//! defaults, so an imported sound plays at a usable volume.
		static bool parseBinary(void const * data, std::size_t size,
			SfxDesc & out, String * outError = NULL);

		//! @brief regenerate canonical `.osfx` text from a description - the
		//! clean-format inverse of parse(). Emits `version 1` then only the
		//! fields that differ from the plain defaults, one per line in the
		//! grammar's declaration order. The output round-trips:
		//! parse(serialize(d)) reproduces d. NOTE it never emits a `preset`
		//! line - a serialized description is the RESOLVED numbers, which is
		//! what makes the round trip exact (and what an imported `.sfs`
		//! becomes when it is written out as text).
		static String serialize(SfxDesc const & desc);

		//! @brief the text the editor's Create > Sound writes for an
		//! archetype: a `preset` line plus that archetype's resolved numbers as
		//! explicit directives, so the designer sees (and can tune) every value
		//! instead of an opaque one-liner.
		static String templateFor(SfxPreset::Archetype archetype,
			unsigned int seed = 1u);
	};

	/** @} */
}

#endif //__SfxAsset_h__29_7_2026__10_20_00__
