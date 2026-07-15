/********************************************************************
	created:	Monday 2010/09/06 at 16:19
	filename: 	SoundPlatform.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "engine_sound/SoundPlatform.h"

#ifdef ORKIGE_OPENAL_SOUND

namespace Orkige
{
	namespace SoundUtil
	{
		ALvoid  alBufferDataPlatform(const ALuint bid, ALenum format, ALvoid* data, ALsizei size, ALsizei freq)
		{
			// OpenAL Soft everywhere: plain alBufferData copies the samples
			// into the buffer, so callers may free their data afterwards.
			// The Apple-only alBufferDataStatic extension (which required the
			// caller to keep the data alive) does not exist in OpenAL Soft.
			alBufferData(bid, format, data, size, freq);
		}
	}
}

#endif //ORKIGE_OPENAL_SOUND
