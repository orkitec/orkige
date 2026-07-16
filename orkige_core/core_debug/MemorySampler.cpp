/**************************************************************
	created:	2026/07/10 at 16:00
	filename: 	MemorySampler.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "core_debug/MemorySampler.h"

#if defined(__EMSCRIPTEN__)
#	include <emscripten/heap.h>
#elif defined(__APPLE__)
#	include <mach/mach.h>
#elif defined(_WIN32)
#	include <windows.h>
#	include <psapi.h>
#elif defined(__linux__)
#	include <cstdio>
#	include <unistd.h>
#endif

namespace Orkige
{
	namespace MemorySampler
	{
		//---------------------------------------------------------
		std::size_t residentBytes()
		{
#if defined(__EMSCRIPTEN__)
			// the browser gives a wasm module no OS process to query; the
			// linear-memory heap size is the process-footprint analog (it
			// only ever grows, and it is what the page actually commits)
			return emscripten_get_heap_size();
#elif defined(__APPLE__)
			// the kernel's own per-task accounting (covers macOS and iOS):
			// resident_size is the physical footprint in bytes
			mach_task_basic_info info{};
			mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
			if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
				reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS)
			{
				return 0;
			}
			return static_cast<std::size_t>(info.resident_size);
#elif defined(_WIN32)
			// K32GetProcessMemoryInfo is exported straight from kernel32, so
			// this needs no extra import library; WorkingSetSize is the RSS
			PROCESS_MEMORY_COUNTERS counters{};
			if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &counters,
				sizeof(counters)))
			{
				return 0;
			}
			return static_cast<std::size_t>(counters.WorkingSetSize);
#elif defined(__linux__)
			// /proc/self/statm: total then resident, both in pages (this path
			// also serves Android, which is Linux underneath)
			std::FILE* statm = std::fopen("/proc/self/statm", "r");
			if (!statm)
			{
				return 0;
			}
			unsigned long totalPages = 0;
			unsigned long residentPages = 0;
			const int read = std::fscanf(statm, "%lu %lu",
				&totalPages, &residentPages);
			std::fclose(statm);
			if (read != 2)
			{
				return 0;
			}
			const long pageSize = sysconf(_SC_PAGESIZE);
			if (pageSize <= 0)
			{
				return 0;
			}
			return static_cast<std::size_t>(residentPages) *
				static_cast<std::size_t>(pageSize);
#else
			return 0;	// no query on this platform - honest "unavailable"
#endif
		}
	}
}
