/********************************************************************
	created:	Monday 2010/09/06 at 16:19
	filename: 	SoundPlatform.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
*********************************************************************/
#ifndef ORKIGE_OGGSOUNDMANAGER
#include "engine_sound/SoundPlatform.h"

#ifdef ORKIGE_OPENAL_SOUND

#ifdef ORKIGE_IPHONE
typedef ALvoid	AL_APIENTRY	(*alBufferDataStaticProcPtr) (const ALint bid, ALenum format, ALvoid* data, ALsizei size, ALsizei freq);
#endif

namespace Orkige
{
	namespace SoundUtil
	{
		ALvoid  alBufferDataPlatform(const ALint bid, ALenum format, ALvoid* data, ALsizei size, ALsizei freq)
		{
#ifdef ORKIGE_IPHONE
			static	alBufferDataStaticProcPtr	proc = NULL;

			if (proc == NULL) {
				proc = (alBufferDataStaticProcPtr) alcGetProcAddress(NULL, (const ALCchar*) "alBufferDataStatic");
			}

			if (proc)
				proc(bid, format, data, size, freq);

			return;
#else
			alBufferData(bid, format, data, size, freq);
#endif
		}
	}
}

#endif //ORKIGE_OPENAL_SOUND
#endif //ORKIGE_OGGSOUNDMANAGER
