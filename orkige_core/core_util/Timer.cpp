/**************************************************************
	created:	2010/08/21 at 13:46
	filename: 	Timer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/

#include "core_util/Timer.h"

#if defined( __WIN32__ ) || defined( _WIN32 )

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cassert>

namespace Orkige
{
	namespace Timer
	{
		namespace
		{
			static LARGE_INTEGER frequency, startTime;
			static DWORD startTick = 0;
		}

		void initialise()
		{
			QueryPerformanceFrequency(&frequency);
			QueryPerformanceCounter(&startTime);
			startTick = GetTickCount();
		}

		unsigned long getMilliseconds()
		{
			// check for initialisation
			assert(startTick != 0);

			LARGE_INTEGER currentTime;
			QueryPerformanceCounter(&currentTime);

			// scale by 1000 for milliseconds
			unsigned long newTicks = (unsigned long)(1000 * (currentTime.QuadPart - startTime.QuadPart) / frequency.QuadPart);

			// detect and compensate for performance counter leaps
			// (surprisingly common, see Microsoft KB: Q274323)
			unsigned long check = GetTickCount() - startTick;
			signed long errorMs = (signed long)(newTicks - check);
			if (errorMs < -100 || errorMs > 100)
			{
				startTime.QuadPart += errorMs * frequency.QuadPart / 1000;
				newTicks = check;
			}

			return newTicks;
		}
	}
}
#else // assume that anything non windows supports the unix gettimeofday() function (Macs do)

/* Code by Sigfried McWild for *nix compatibility */

#include <sys/time.h>


#include <cassert>
namespace Orkige
{
	// I'm not quite sure if I'm doing this the right way since I don't really know the win32 api and what all those calls above do
	// I'll do my best anyway
	namespace Timer
	{
		// don't really like this stuff with globals lying around but I guess it is the best way to do it in this case
		namespace
		{
			unsigned long startTick = 0;
			timeval currentTime; // so I don't have to allocate them over and over
		}

		void initialise()
		{
			gettimeofday(&currentTime, 0);
			startTick = currentTime.tv_sec * 1000000 + currentTime.tv_usec; // ticks are stored in microseconds (potentially)
		}

		unsigned long getMilliseconds()
		{
			// check for init
			assert(startTick != 0);

			gettimeofday(&currentTime, 0);

			// let's not worry about the counter leaps, I'm pretty sure unix does not suffer from those issues
			// pbridger: my understanding was that it was a hardware issue, but I'm no expert
			return (currentTime.tv_sec * 1000000 + currentTime.tv_usec - startTick) / 1000;
		}
	}
}
#endif