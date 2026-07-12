/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	MemoryManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __MemoryManager_h__12_7_2026__10_00_00__
#define __MemoryManager_h__12_7_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"

#include <cstddef>

namespace Orkige
{
	//! @brief per-subsystem, per-frame allocation counters - the engine-level
	//! churn instrument (MemorySampler is the complementary process-level RSS
	//! number). OPT-IN by design: the engine's own allocation seams (event
	//! queue pushes, container growth in the gui/physics/tween/particle hot
	//! paths) call countAlloc/countGrowth with their subsystem tag; nothing
	//! hooks global operator new/delete, so third-party allocators and
	//! static-init order stay untouched and untracked code costs nothing.
	//! @remarks The unit is TRACKED ALLOCATION EVENTS - one count per time an
	//! instrumented seam heap-allocates (a queue node, a vector buffer grow) -
	//! not bytes and not libc totals. That is exactly the currency the perf
	//! contracts trade in: a steady-state frame in an instrumented subsystem
	//! asserts lastFrameCount(tag) == 0 directly. Lua's own VM churn is
	//! invisible here by design (it allocates through the Lua allocator, not
	//! through an engine seam).
	//! @remarks Thread-aware: the running counts are relaxed atomics, so
	//! worker threads (physics contact callbacks) merge safely. endFrame() -
	//! called once per frame at the canonical player-tick frame boundary -
	//! folds the running counts into the last-frame snapshot, tracks the
	//! per-frame peak and resets for the next frame. Hosts that never call
	//! endFrame (the editor) just accumulate counts nobody reads - harmless.
	namespace MemoryManager
	{
		//! the instrumented subsystems. A tag exists only where a real
		//! per-frame hot path has a countable seam - a tag nobody reads is
		//! noise. TAG_OTHER is the home for future one-off seams.
		enum AllocTag
		{
			TAG_EVENTS,		//!< event-bus queue nodes + script event objects
			TAG_GUI,		//!< gui rebuild vertex-scratch growth
			TAG_PHYSICS,	//!< contact queue / frame-contact buffer growth
			TAG_TWEENS,		//!< tween list growth
			TAG_PARTICLES,	//!< particle pool / vertex-scratch growth
			TAG_OTHER,		//!< untagged one-off seams
			TAG_COUNT		//!< number of tags (array bound, not a tag)
		};

		//! @brief count allocation events against a tag; callable from any
		//! thread (relaxed atomic add), any time (no setup required)
		ORKIGE_CORE_DLL void countAlloc(AllocTag tag, std::size_t count = 1);

		//! @brief count a container growth: pass the container's capacity
		//! before and after a mutation - a changed capacity is one tracked
		//! allocation event (the buffer was reallocated)
		inline void countGrowth(AllocTag tag, std::size_t capacityBefore,
			std::size_t capacityAfter)
		{
			if (capacityAfter != capacityBefore)
			{
				countAlloc(tag);
			}
		}

		//! @brief the frame boundary: snapshot the running counts as the
		//! last-frame values, fold the per-frame peak and reset the running
		//! counts. Call ONCE per frame from the main thread, at a point where
		//! instrumented worker activity is quiescent (the canonical player
		//! tick's frame boundary qualifies - physics jobs complete inside the
		//! physics update).
		ORKIGE_CORE_DLL void endFrame();

		//! counts recorded so far in the RUNNING frame (test introspection)
		ORKIGE_CORE_DLL std::size_t currentCount(AllocTag tag);
		//! the tag's count in the last completed frame
		ORKIGE_CORE_DLL std::size_t lastFrameCount(AllocTag tag);
		//! all tags summed, last completed frame
		ORKIGE_CORE_DLL std::size_t lastFrameTotal();
		//! the worst lastFrameTotal seen since reset()
		ORKIGE_CORE_DLL std::size_t peakFrameTotal();
		//! completed frames since reset() (0 = no endFrame yet, values unset)
		ORKIGE_CORE_DLL std::size_t framesSampled();

		//! the tag's short lowercase name ("events", "gui", ...)
		ORKIGE_CORE_DLL const char * tagName(AllocTag tag);

		//! zero everything - running counts, snapshots, peak, frame count
		ORKIGE_CORE_DLL void reset();
	}
}

#endif //__MemoryManager_h__12_7_2026__10_00_00__
