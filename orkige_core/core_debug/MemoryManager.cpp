/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	MemoryManager.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "core_debug/MemoryManager.h"

#include <atomic>

namespace Orkige
{
	namespace MemoryManager
	{
		namespace
		{
			//! running counts of the current frame - the only cells touched
			//! by countAlloc, so a count from any thread is one relaxed add
			std::atomic<std::size_t> sRunning[TAG_COUNT];

			//! the last completed frame's counts; written by endFrame and
			//! read on the main thread only (the readback contract)
			std::size_t sLast[TAG_COUNT];
			std::size_t sLastTotal = 0;
			std::size_t sPeakTotal = 0;
			std::size_t sFrames = 0;

			const char * const sTagNames[TAG_COUNT] =
			{
				"events", "gui", "physics", "tweens", "particles", "other"
			};
		}
		//---------------------------------------------------------
		void countAlloc(AllocTag tag, std::size_t count)
		{
			if (tag < 0 || tag >= TAG_COUNT)
			{
				tag = TAG_OTHER;
			}
			sRunning[tag].fetch_add(count, std::memory_order_relaxed);
		}
		//---------------------------------------------------------
		void endFrame()
		{
			std::size_t total = 0;
			for (int tag = 0; tag < TAG_COUNT; ++tag)
			{
				sLast[tag] = sRunning[tag].exchange(0,
					std::memory_order_relaxed);
				total += sLast[tag];
			}
			sLastTotal = total;
			if (total > sPeakTotal)
			{
				sPeakTotal = total;
			}
			++sFrames;
		}
		//---------------------------------------------------------
		std::size_t currentCount(AllocTag tag)
		{
			if (tag < 0 || tag >= TAG_COUNT)
			{
				return 0;
			}
			return sRunning[tag].load(std::memory_order_relaxed);
		}
		//---------------------------------------------------------
		std::size_t lastFrameCount(AllocTag tag)
		{
			if (tag < 0 || tag >= TAG_COUNT)
			{
				return 0;
			}
			return sLast[tag];
		}
		//---------------------------------------------------------
		std::size_t lastFrameTotal()
		{
			return sLastTotal;
		}
		//---------------------------------------------------------
		std::size_t peakFrameTotal()
		{
			return sPeakTotal;
		}
		//---------------------------------------------------------
		std::size_t framesSampled()
		{
			return sFrames;
		}
		//---------------------------------------------------------
		const char * tagName(AllocTag tag)
		{
			if (tag < 0 || tag >= TAG_COUNT)
			{
				return "other";
			}
			return sTagNames[tag];
		}
		//---------------------------------------------------------
		void reset()
		{
			for (int tag = 0; tag < TAG_COUNT; ++tag)
			{
				sRunning[tag].store(0, std::memory_order_relaxed);
				sLast[tag] = 0;
			}
			sLastTotal = 0;
			sPeakTotal = 0;
			sFrames = 0;
		}
	}
}
