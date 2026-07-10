/**************************************************************
	created:	2026/07/08 at 21:00
	filename: 	EngineLog.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EngineLog_h__8_7_2026__21_00_00__
#define __EngineLog_h__8_7_2026__21_00_00__
#include "core_util/optr.h"
#include "core_module/OrkigePrerequisites.h"
#include <core_util/String.h>

#include <memory>
#include <vector>


//! same export rule as engine_module/EnginePrerequisites.h - duplicated
//! (guarded) so this header stays free of the Ogre-including umbrella;
//! inert while ORKIGE_STATIC builds everything statically
#ifndef ORKIGE_ENGINE_DLL
#	ifdef WIN32
#		if defined( ORKIGE_STATIC ) || defined( __MINGW32__ )
#			define ORKIGE_ENGINE_DLL
#		else
#			ifdef orkige_engine_EXPORTS
#				define ORKIGE_ENGINE_DLL __declspec( dllexport )
#			else
#				define ORKIGE_ENGINE_DLL __declspec( dllimport )
#			endif
#		endif
#	else // Linux / Mac OSX etc
#		define ORKIGE_ENGINE_DLL
#	endif
#endif //ORKIGE_ENGINE_DLL

namespace Orkige
{
	//! @brief the engine log as a SERVICE: capture the engine's default log
	//! stream (line + severity) and write lines into it - without exposing
	//! the logging backend (today OGRE's LogManager, an implementation
	//! detail behind this header per Docs/render-abstraction.md: log
	//! forwarding is an engine service, not part of the render facade).
	//! @remarks First consumer is PlayerDebugLink (engine_runtime), which
	//! forwards captured lines to the editor Console as "[remote]" lines;
	//! the editor's own console capture migrates onto this.
	//! Captured lines queue in a bounded backlog (oldest lines drop on
	//! overflow - the newest context is the interesting part) and are
	//! drained by the consumer once per frame. Capture may fire off the
	//! main thread, so the backlog is mutex-guarded internally.
	class ORKIGE_ENGINE_DLL EngineLogCapture
	{
		//--- Types -------------------------------------------
	public:
		//! one captured log line: severity ("info"/"warning"/"error") + text
		struct Line
		{
			String	level;
			String	text;
		};
		//! default backlog cap - protects consumers from unbounded buffering
		static const size_t DEFAULT_BACKLOG_LINES;
	protected:
		//! backend listener state (hides the logging library)
		struct Impl;
	private:
		//--- Variables ---------------------------------------
	public:
	protected:
		Orkige::uptr<Impl>	mImpl;
	private:
		//--- Methods -----------------------------------------
	public:
		EngineLogCapture(size_t backlogLineCap = DEFAULT_BACKLOG_LINES);
		~EngineLogCapture();	//!< detaches when still attached

		//! @brief start capturing the engine's default log; false when no
		//! engine log exists yet (call after the engine is up). Idempotent.
		bool attach();
		//! stop capturing (safe when never attached)
		void detach();
		bool isAttached() const;
		//! @brief take all lines captured since the last drain (call once
		//! per frame; thread-safe against the capture side)
		std::vector<Line> drain();

		//! @brief write a line into the engine's default log (attached
		//! captures - including this one - see it); no-op without a log
		static void logMessage(String const & text);
		//! @brief write an ERROR line into the engine's default log; falls
		//! back to stderr when no engine log exists (headless runs) - errors
		//! must stay visible in every build configuration
		static void logError(String const & text);
	protected:
	private:
		EngineLogCapture(EngineLogCapture const &);					// non-copyable
		EngineLogCapture & operator=(EngineLogCapture const &);	// non-copyable
	};
}

#endif //__EngineLog_h__8_7_2026__21_00_00__
