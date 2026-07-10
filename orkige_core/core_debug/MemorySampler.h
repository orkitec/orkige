/**************************************************************
	created:	2026/07/10 at 16:00
	filename: 	MemorySampler.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __MemorySampler_h__10_7_2026__16_00_00__
#define __MemorySampler_h__10_7_2026__16_00_00__

#include "core_module/OrkigePrerequisites.h"

#include <cstddef>

namespace Orkige
{
	//! @brief the process's memory footprint, queried from the operating
	//! system - the always-available runtime memory metric (works in Release
	//! as well as Debug, on desktop and mobile alike). The number is the
	//! resident set size: the physical RAM the process currently occupies, the
	//! honest answer to "is the running game growing?".
	//! @remarks Backed by the platform's own accounting - task_info on
	//! Apple platforms, /proc/self/statm on Linux/Android, GetProcessMemoryInfo
	//! on Windows - so it carries no per-allocation overhead and needs no
	//! global new/delete override. A platform without a query returns 0
	//! ("unavailable"), never a fabricated value; callers surface that as n/a.
	namespace MemorySampler
	{
		//! @brief the process's current resident set size in bytes, or 0 when
		//! the platform offers no way to query it. A cheap OS call - sampling
		//! it every frame is fine, but a few times a second is plenty (the
		//! number moves slowly).
		ORKIGE_CORE_DLL std::size_t residentBytes();
	}
}

#endif //__MemorySampler_h__10_7_2026__16_00_00__
