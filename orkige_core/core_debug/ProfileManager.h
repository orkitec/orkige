/**************************************************************
	created:	2026/07/12 at 10:00
	filename: 	ProfileManager.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ProfileManager_h__12_7_2026__10_00_00__
#define __ProfileManager_h__12_7_2026__10_00_00__

#include "core_module/OrkigePrerequisites.h"

#include <cstddef>
#include <vector>

namespace Orkige
{
	//! @brief the hierarchical per-frame CPU profiler behind the OPROFILE /
	//! OPROFILEFUNC scope macros (core_debug/Profile.h): nested scopes build a
	//! call tree per thread, each node accumulating call count and wall time
	//! (steady clock) for the running frame; endFrame() - the canonical
	//! player-tick frame boundary - folds the accumulators into a last-frame
	//! snapshot the readback surfaces (editor Stats panel, MSG_PROFILE_DATA,
	//! the trace, MCP get_profile) present as "where the frame went".
	//! @remarks The instrument must not pollute its own measurement: a node is
	//! allocated once on the scope's FIRST visit and reused forever after, the
	//! per-thread stack is the node tree itself (parent pointers), and scope
	//! names must be STATIC strings (string literals) - lookups compare
	//! pointers first and never copy. Steady state is allocation-free.
	//! @remarks Always compiled; runtime-gated by setEnabled. Debug builds
	//! start enabled, Release builds start disabled - a disabled scope costs
	//! one relaxed atomic load and a branch (so shipping code keeps its
	//! OPROFILE sites). Frame TIMING (lastFrameMilliseconds) is measured by
	//! endFrame regardless of the gate - it is one clock read per frame.
	//! @remarks Threads: each thread owns its own tree (a scope on a worker
	//! thread can never corrupt the main tree); the first thread to open a
	//! scope is labeled "main". endFrame/snapshot walk every thread's tree
	//! and require instrumented worker scopes to be CLOSED at the frame
	//! boundary (true in the engine: physics jobs complete inside the physics
	//! update). Direct recursion accumulates into one node, counted once.
	namespace ProfileManager
	{
		//! one node of the flattened snapshot tree, depth-first order
		struct SnapshotNode
		{
			const char *	name;				//!< the scope's static name
			int				depth;				//!< 0 = a frame-level scope
			unsigned int	calls;				//!< calls in the last frame
			double			milliseconds;		//!< time in the last frame
			double			maxMilliseconds;	//!< worst frame since reset
		};

		//! gate the scope machinery (Debug default: on, Release default: off)
		ORKIGE_CORE_DLL void setEnabled(bool enabled);
		ORKIGE_CORE_DLL bool isEnabled();

		//! @brief open a scope on the calling thread's tree. name MUST be a
		//! static string (a literal). Returns false when disabled - the
		//! Profile RAII only closes scopes it actually opened, so toggling
		//! the gate mid-frame stays balanced.
		ORKIGE_CORE_DLL bool beginScope(const char * name);
		//! close the calling thread's innermost open scope
		ORKIGE_CORE_DLL void endScope();

		//! @brief the frame boundary: fold every thread's running
		//! accumulators into the last-frame snapshot values and measure the
		//! frame duration. Call ONCE per frame from the main thread, with all
		//! scopes closed.
		ORKIGE_CORE_DLL void endFrame();

		//! wall time between the last two endFrame calls (0 before frame 2)
		ORKIGE_CORE_DLL double lastFrameMilliseconds();
		//! endFrame calls since reset()
		ORKIGE_CORE_DLL unsigned long framesSampled();

		//! @brief flatten the last-frame tree into out (cleared first),
		//! depth-first in scope-creation order: the "main" thread's scopes
		//! start at depth 0; every other thread that recorded scopes
		//! contributes a "thread:<label>" row at depth 0 with its scopes
		//! below. The out vector is the CALLER's allocation, reusable.
		ORKIGE_CORE_DLL void snapshot(std::vector<SnapshotNode> & out);

		//! drop all nodes and counters on every thread's tree (tests); only
		//! call with no scopes open anywhere
		ORKIGE_CORE_DLL void reset();
	}
}

#endif //__ProfileManager_h__12_7_2026__10_00_00__
