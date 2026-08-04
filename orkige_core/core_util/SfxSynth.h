/********************************************************************
	created:	Wednesday 2026/07/29 at 10:10
	filename: 	SfxSynth.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __SfxSynth_h__29_7_2026__10_10_00__
#define __SfxSynth_h__29_7_2026__10_10_00__

//! @file SfxSynth.h
//! @brief the pure procedural sound-effect synthesizer: a standard-parameter
//! SfxDesc in, 16-bit mono PCM out
//! @remarks No audio backend, no engine, no allocation beyond the one result
//! buffer - the GradeMath / CameraFit / UiLayout shape, so the whole sound
//! character of a sound asset is asserted headlessly in unit tests. The engine
//! side hands the returned samples to the SAME mixer voice a `.wav`
//! goes through (SoundUtil::loadSoundData -> alBufferDataPlatform), so a
//! synthesized sound is a sound source like any other: positional, mixer
//! grouped, pitch/volume varied, interruption-safe.
//!
//! THE ALGORITHM is the standard retro-sfx one (@see SfxDesc.h on provenance,
//! Docs/sound.md for the full note): a single oscillator whose period slides
//! and is arpeggio-stepped, an attack/sustain/punch/decay envelope, a resonant
//! one-pole low-pass and a one-pole high-pass with sweeps, a 1024-sample
//! swept delay tap, a retrigger, and 8x supersampling per sample. It runs at a
//! FIXED 44100 Hz because the standard's envelope and period constants are
//! expressed in samples at that rate - rendering at another rate would change
//! the sound, so the rate is not a parameter.
//!
//! DETERMINISM is a contract: the noise oscillator (and the archetype
//! generators) run off SfxDesc::seed through an explicit integer generator,
//! never a std:: distribution whose sequence is implementation-defined. One
//! stored effect therefore yields byte-identical samples on every load, which
//! is what makes a rendered sound assertable.
//!
//! LEVELS: the mix is clamped to full scale exactly where the standard clamps
//! it, so the buffer never wraps or goes non-finite; a high punch value drives
//! the signal INTO that clamp on purpose - it is part of the sound. sanitize()
//! keeps every parameter inside the standard's own range and names what it
//! moved, so an absurd number is corrected and reported rather than rendered.

#include "core_util/SfxDesc.h"

#include <cstdint>
#include <vector>

namespace Orkige
{
	/** \addtogroup Util
	*  @{ */

	//! @brief the standard parameter set -> PCM samples synthesizer (pure,
	//! headless)
	class SfxSynth
	{
	public:
		//--- Types -------------------------------------------------
		//! @brief a rendered effect: 16-bit signed mono samples plus the rate
		//! they were rendered at (the pair a mixer voice needs)
		struct Pcm
		{
			std::vector<std::int16_t>	samples;			//!< mono
			int							sampleRate = 44100;	//!< always SAMPLE_RATE
			int							channels = 1;		//!< always 1 (mono, so a source is positional)
			int							bitsPerSample = 16;	//!< always 16

			//! the byte size of the sample block (what alBufferData takes)
			std::size_t byteSize() const
			{
				return this->samples.size() * sizeof(std::int16_t);
			}
			//! the rendered length in seconds (0 for an empty buffer)
			float durationSec() const
			{
				return (this->sampleRate > 0)
					? static_cast<float>(this->samples.size()) /
						static_cast<float>(this->sampleRate)
					: 0.0f;
			}
		};

		//--- Constants ---------------------------------------------
		//! the standard's render rate - see the file comment on why it is fixed
		static const int SAMPLE_RATE;			//!< 44100
		//! the supersampling factor the standard mixes at
		static const int SUPERSAMPLES;			//!< 8
		//! @brief a hard ceiling on the rendered sample count (a safety net,
		//! not a policy: the parameter ranges alone bound an effect to about
		//! seven seconds)
		static const std::size_t MAX_SAMPLES;	//!< 44100 * 12

		//--- Methods -----------------------------------------------
		//! @brief clamp every field of @p desc into the standard's range,
		//! naming each correction.
		//! @param outNotes when given, receives one human-readable line per
		//! clamped field ("baseFreq 4 clamped to 1") - the caller logs them as
		//! warnings (this stays a pure function; core_util never logs).
		//! @return true when nothing needed clamping (@p desc was already sane)
		//! @remarks Out-of-range input is CORRECTED, never rejected: a
		//! mistyped weight still makes a sound and says so, because a silent
		//! asset is the harder bug to find. A non-finite value (a NaN that
		//! survived a parse) falls back to the field's default.
		static bool sanitize(SfxDesc & desc,
			std::vector<String> * outNotes = NULL);

		//! @brief synthesize @p desc into 16-bit mono PCM at SAMPLE_RATE.
		//! @remarks Sanitizes a COPY first (the caller's desc is never
		//! mutated; call sanitize() yourself when you want the notes), so
		//! render() always returns a finite, non-wrapping, at-least-one-sample
		//! buffer.
		static Pcm render(SfxDesc const & desc);

		//! @brief the length render() will produce for @p desc, in seconds
		//! (after sanitizing) - the audition UI's progress reference.
		//! @remarks Derived from the envelope's own sample counts, so it needs
		//! no render; a retrigger does not extend an effect (it restarts the
		//! oscillator, not the envelope). It is the envelope's length, so it is
		//! an UPPER bound: a freqLimit the slide reaches ends playback early.
		static float renderedDuration(SfxDesc const & desc);

	private:
		SfxSynth() = delete;
	};

	/** @} */
}

#endif //__SfxSynth_h__29_7_2026__10_10_00__
