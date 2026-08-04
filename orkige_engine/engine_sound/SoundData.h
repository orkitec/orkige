/********************************************************************
	created:	Monday 2010/09/06 at 16:19
	filename: 	SoundData.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	The decode seam: a sound FILE turned into a block of PCM
*********************************************************************/

#ifndef __SoundData_h__6_9_2010__16_19_22__
#define __SoundData_h__6_9_2010__16_19_22__

#include "engine_sound/AudioBackend.h"
#include <core_util/StringUtil.h>

namespace Orkige
{
	//! sound utilities
	namespace SoundUtil
	{
		//! how a loader describes the block it produced
		typedef AudioBackend::PcmFormat PcmFormat;

		//! load caf audio, only works on Apple platforms; NULL elsewhere
		void* loadCafData(Orkige::String const & fileName, int * dataSize,
			PcmFormat * format);
		//! load wav audio data
		void* loadWavData(Orkige::String const & fileName, int * dataSize,
			PcmFormat * format);
		//! @brief SYNTHESIZE a procedural sound effect from a PARAMETER file -
		//! the standard binary `.sfs` or its `.osfx` text twin. The
		//! decoder-shaped sibling of the wave loaders (@see LoadSfxData.cpp,
		//! core_util/SfxAsset.h)
		void* loadSfxData(Orkige::String const & fileName, int * dataSize,
			PcmFormat * format);
		//! @brief load audio data depending on fileName extension
		//! @remarks the ONE place a sound file is turned into samples; the
		//! caller owns the returned block and frees it with free()
		static inline void* loadSoundData(Orkige::String const & fileName,
			int * dataSize, PcmFormat * format)
		{
			if(StringUtil::to_lower_copy(fileName).ends_with(".wav"))
			{
				return loadWavData(fileName, dataSize, format);
			}
			else if(StringUtil::to_lower_copy(fileName).ends_with(".osfx") ||
				StringUtil::to_lower_copy(fileName).ends_with(".sfs"))
			{
				// a procedural effect: nothing to decode, the samples are
				// SYNTHESIZED from the file's sound parameters
				return loadSfxData(fileName, dataSize, format);
			}
			else if(StringUtil::to_lower_copy(fileName).ends_with(".caf"))
			{
#ifndef __APPLE__
				return NULL;
#else
				return loadCafData(fileName, dataSize, format);
#endif
			}
			else
			{
				oAssertDesc(!"Unknown SoundFile extension!", "Extension unsupported for file: " << fileName);
			}
			return NULL;
		}
	}
	//---------------------------------------------------------------
}
#endif //__SoundData_h__6_9_2010__16_19_22__
