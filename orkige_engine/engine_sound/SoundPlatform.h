/********************************************************************
	created:	Monday 2010/09/06 at 16:19
	filename: 	SoundPlatform.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
	
	purpose:	Platform specific sound handling
*********************************************************************/

#ifndef __SoundPlatform_h__6_9_2010__16_19_22__
#define __SoundPlatform_h__6_9_2010__16_19_22__

#include "engine_module/EnginePrerequisites.h"
#ifdef ORKIGE_IPHONE
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <al.h>
#include <alc.h>
#endif
#include <boost/algorithm/string.hpp>

#ifdef ORKIGE_OGGSOUNDMANAGER
#include <OgreOggSound.h>
#include <OgreOggSoundPlugin.h>
#else
namespace Orkige
{
	//! sound utilities
	namespace SoundUtil
	{
		//!platform specific alBufferData method
		ALvoid  alBufferDataPlatform(const ALint bid, ALenum format, ALvoid* data, ALsizei size, ALsizei freq);
		//! load caf audio only work on OSX/iPhone returns NULL on other platforms
		void* loadCafData(Orkige::String const & fileName, ALsizei *dataSize, ALenum *dataFormat, ALsizei* sampleRate);
		//! load wav audio data
		void* loadWavData(Orkige::String const & fileName, ALsizei *dataSize, ALenum *dataFormat, ALsizei* sampleRate);
		//! load audio data depending on fileName extension
		static inline void* loadSoundData(Orkige::String const & fileName, ALsizei *dataSize, ALenum *dataFormat, ALsizei* sampleRate)
		{
			if(boost::ends_with(boost::to_lower_copy(fileName), ".wav"))
			{
				return loadWavData(fileName, dataSize, dataFormat, sampleRate);
			}
			else if(boost::ends_with(boost::to_lower_copy(fileName), ".caf"))
			{
#ifndef ORKIGE_IPHONE
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
#endif //ORKIGE_OGGSOUNDMANAGER
#endif //__SoundPlatform_h__6_9_2010__16_19_22__

