/********************************************************************
	created:	Wednesday 2026/07/29 at 14:00
	filename: 	WavWriter.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __WavWriter_h__29_7_2026__14_00_00__
#define __WavWriter_h__29_7_2026__14_00_00__

//! @file WavWriter.h
//! @brief a minimal, dependency-free RIFF/WAVE encoder for 16-bit PCM
//! @remarks The audio twin of PngWriter, and it exists for the same reason: a
//! synthesized sound (SfxSynth) is samples in memory with no file behind it,
//! and a designer sometimes needs the actual audio FILE - to hand to a sound
//! designer, drop into a video editor, or listen to outside the engine. A WAV
//! header is a fixed 44-byte prologue in front of the raw samples, so this
//! costs no dependency at all.
//!
//! It is an EXPORT convenience, never a load path: the engine plays the
//! parameter asset and synthesizes it (a hundred bytes of text, tunable and
//! diffable), so baked audio never enters the runtime.
//!
//! Pure and unit-testable: it builds the byte stream and hands it back. Putting
//! those bytes on disk belongs to the caller - core code reads content through
//! the resource funnel and does not open files of its own, and the one consumer
//! that needs a file (the editor's Export WAV) already owns a write path.

#include <core_util/String.h>

#include <cstdint>
#include <vector>

namespace Orkige
{
	/** \addtogroup Util
	*  @{ */

	//! @brief 16-bit PCM samples -> a RIFF/WAVE byte stream (@see the file doc)
	class WavWriter
	{
	public:
		//! the bytes a 16-bit PCM WAV header occupies before the samples
		static const std::size_t HEADER_BYTES;	//!< 44

		//! @brief encode interleaved 16-bit PCM into a WAV byte stream
		//! appended to @p out.
		//! @param samples interleaved samples (frames * channels values)
		//! @param sampleRate Hz (must be positive)
		//! @param channels 1 (mono) or 2 (stereo)
		//! @return false on a bad argument (empty samples, non-positive rate,
		//! an unsupported channel count, or a sample count that is not a whole
		//! number of frames) - @p out untouched.
		static bool encode(std::vector<std::int16_t> const & samples,
			int sampleRate, int channels, std::vector<unsigned char> & out);
	};

	/** @} */
}

#endif //__WavWriter_h__29_7_2026__14_00_00__
