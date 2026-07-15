/********************************************************************
	created:	Monday 2010/09/06 at 16:19
	filename: 	SoundPlatform.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	purpose:	Platform specific sound handling
*********************************************************************/

#ifndef __SoundPlatform_h__6_9_2010__16_19_22__
#define __SoundPlatform_h__6_9_2010__16_19_22__

#include "engine_module/EnginePrerequisites.h"
#ifdef ORKIGE_OPENAL_SOUND

// OpenAL Soft (vcpkg openal-soft) is the OpenAL implementation on every
// platform now, so the Apple framework layout <OpenAL/al.h> is gone for good.
#include <AL/al.h>
#include <AL/alc.h>
#include <core_util/StringUtil.h>

namespace Orkige
{
	//! sound utilities
	namespace SoundUtil
	{
		//!platform specific alBufferData method
		ALvoid  alBufferDataPlatform(const ALuint bid, ALenum format, ALvoid* data, ALsizei size, ALsizei freq);
		//! load caf audio only works on OSX/iOS returns NULL on other platforms
		void* loadCafData(Orkige::String const & fileName, ALsizei *dataSize, ALenum *dataFormat, ALsizei* sampleRate);
		//! load wav audio data
		void* loadWavData(Orkige::String const & fileName, ALsizei *dataSize, ALenum *dataFormat, ALsizei* sampleRate);
		//! load audio data depending on fileName extension
		static inline void* loadSoundData(Orkige::String const & fileName, ALsizei *dataSize, ALenum *dataFormat, ALsizei* sampleRate)
		{
			if(StringUtil::to_lower_copy(fileName).ends_with(".wav"))
			{
				return loadWavData(fileName, dataSize, dataFormat, sampleRate);
			}
			else if(StringUtil::to_lower_copy(fileName).ends_with(".caf"))
			{
#ifndef __APPLE__
				return NULL;
#else
				return loadCafData(fileName, dataSize, dataFormat, sampleRate);
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
#endif //ORKIGE_OPENAL_SOUND
#endif //__SoundPlatform_h__6_9_2010__16_19_22__
