/**************************************************************
	created:	2026/07/16 at 02:30
	filename: 	MainLoopPacer.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __MainLoopPacer_h__16_7_2026__02_30_00__
#define __MainLoopPacer_h__16_7_2026__02_30_00__

#ifdef __EMSCRIPTEN__
#	include <emscripten.h>
#endif

namespace Orkige
{
	//! @brief the host-yield seam at the bottom of an app's frame loop
	//! @remarks A native window system preempts the process between frames,
	//! so a `while (running)` loop is well-formed as written - on those
	//! platforms yieldToHost() is an empty inline function (no codegen, no
	//! behavior change). The browser's event loop is cooperative instead: a
	//! frame must hand control back to the page or the tab hangs, so the
	//! wasm build suspends the loop right here - Emscripten's ASYNCIFY
	//! transform (the player links with -sASYNCIFY) unwinds the live wasm
	//! stack into a side buffer, returns to the page, and rewinds the loop
	//! in place on the next event-loop turn. The suspension preserves every
	//! stack frame, so locals, RAII lifetimes and the orderly teardown after
	//! the loop all run exactly as the source says. The cost is carried by
	//! the wasm binary alone: ASYNCIFY instruments every call path that can
	//! reach a suspend point (a larger module, some CPU overhead there).
	namespace MainLoopPacer
	{
#ifdef __EMSCRIPTEN__
		inline void yieldToHost()
		{
			// a zero-delay sleep parks the loop on the page's timer queue
			// and resumes on the next event-loop turn
			emscripten_sleep(0);
		}
#else
		inline void yieldToHost() {}
#endif
	}
}

#endif //__MainLoopPacer_h__16_7_2026__02_30_00__
