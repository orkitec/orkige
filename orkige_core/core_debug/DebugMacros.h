/**************************************************************
        created:	2010/09/08 at 10:15
        filename: 	DebugMacros.h
        author:		steffen.roemer
        notice:		This source file is part of orkige (orkitec Game engine)
                                For the latest info, see http://www.orkitec.com/
        copyright:	(c) 2009-2026 orkitec

        based on the work by William E. Kempf. on http://www.codeproject.com/debug/debug_macros.asp

        Description of available macros:

        oAssert(f)			: Breaks if f gives false
        oVerify(x)			: same as oAssert but breaks only in _DEBUG builds
        oPopup(msg)			: gives an oAssert Popup and a oInfo Message msg has to
                                                  be true!!! breaks only in _DEBUG builds
        oPopupTrace(msg)	: gives an oAssert Popup and a oTrace Message msg has to
                                                  be true!!! breaks only in _DEBUG builds
        oTrace(msg)			: Trace msg from output channel.
        oInfo(msg)			: Similar to oTrace but includes a preamble specifying the
                                                  file name and line number where the INFO is called as
                                                  well as forces a line break at the end.
        oDoubleInfo(a,b)	: same as oInfo but you can add a Variable by wildcard
                                                  %s,%i etc similar to printf
        oDebugCode(Code)	: Code contained within this macro is included only in
                                                  _DEBUG builds.
        oDebugBreak()		: Forces a break in _DEBUG builds via DebugBreak.
        oIsNull(expr)		: checks if expr isNULL


        oNotify(message)	: logs and traces with priority NOTIFY
        oWarning(message)	: logs and traces with priority WARNING
        oError(message)		: logs and traces with priority ERROR
        oDebugMsg(tag, level, message)	: logs and traces message if tag is
                                                                          enabled and level is higher or similar
***************************************************************/
#ifndef __DebugMacros_h__8_9_2010__10_15_05__
#define __DebugMacros_h__8_9_2010__10_15_05__

/**
* @defgroup Debug Debug
* @{
* Debugging functions and macros
* @} End of "defgroup Debug".
*/

#ifdef ORKIGE_DEBUG
#	ifdef WIN32
#	ifndef _CRTDBG_MAP_ALLOC
#		define _CRTDBG_MAP_ALLOC
#	endif
#		include <stdlib.h>
#		include <crtdbg.h>
#	endif
#endif //ORKIGE_DEBUG

#include <cstring>
#include <functional>
#include <sstream>
#include <string>
//#undef _DEBUG //test defines

namespace Orkige
{
	/** \addtogroup Debug
	*  @{ */
	//! @brief a log message's severity, low value = louder/more important. A tag
	//! only emits a message whose severity is <= the tag's current threshold.
	enum LogLevel
	{
		LL_ERROR = 0,	//!< a genuine failure (missing resource, failed parse)
		LL_WARN  = 1,	//!< a recoverable problem the developer should see
		LL_INFO  = 2,	//!< high-level progress notes
		LL_DEBUG = 3,	//!< verbose, per-subsystem detail (off until raised)
		LL_OFF   = -1	//!< a tag that emits nothing at all
	};

	//! @brief the fast gate: true when tag currently logs at the given severity.
	//! Cheap enough for a disabled hot path (a single relaxed atomic load rejects
	//! anything above the loudest active threshold before any lock or lookup) - the
	//! logging macros call this BEFORE building the message stream, so a disabled
	//! call never evaluates its arguments.
	bool logTagEnabled(const char* tag, int level);
	//! @brief emit one already-gated line to stderr + the LogManager file sink; an
	//! LL_ERROR additionally drops a Breadcrumbs entry (the crash-survivable trail).
	void logEmit(const char* tag, int level, std::string const & message,
		const char* fileName, int lineNumber);
	//! set a tag's threshold explicitly (the runtime control seam behind log.<tag>)
	void logSetTagLevel(const char* tag, int level);
	//! clear a tag's override so it inherits the process default again
	void logClearTagLevel(const char* tag);
	//! a tag's explicit threshold, or -2 when it inherits the default
	int  logGetTagLevel(const char* tag);
	//! set/get the process-wide default threshold (used by any tag with no override)
	void logSetDefaultLevel(int level);
	int  logGetDefaultLevel();
	//! parse "error"/"warn"/"info"/"debug"/"off" (case-insensitive), -3 = invalid
	int  logLevelFromName(const char* name);
	//! the canonical lower-case name for a level ("error"/"warn"/.../"off")
	const char* logLevelName(int level);
	//! register the log.<tag> + log.default cvars (idempotent; auto-run at startup)
	void logInstallCVars();

	//! extra log sink: a single optional callback handed every EMITTED (already
	//! gated) line, for a consumer that surfaces the diagnostic stream in its
	//! own UI (the editor Console). ONE slot by design - reuse-before-invent, a
	//! subscriber list waits for a second consumer. It runs on the logging
	//! thread AFTER the gate, so a disabled tag/level never reaches it.
	//! LIFETIME: the consumer MUST logClearSink() before its captured state
	//! dies; set/clear and the emit-time call share a mutex, so a clear cannot
	//! return while a call is in flight (no dangling-callback teardown fault).
	using LogSink =
		std::function<void(int level, const char* tag, const char* message)>;
	void logSetSink(LogSink sink);
	void logClearSink();
	/** @} End of "addtogroup Debug"*/
}

//change this if you wan't traceable console output for Notify, Warning and Error in Release build
//normally you don't wan't that! because logging to file happens anyway
#ifndef ORKIGE_DEBUG
        #define NOTRACE
#endif

//For gcc in Windows w codeblocks
#ifdef __GNUC__
    #undef WIN32
        #undef _MFC_VER
#endif

#if _MSC_VER >= 1000
        #pragma once
#endif	// _MSC_VER >= 1000

#ifndef	_MFC_VER
        #ifndef WIN32 //linux
                #include <assert.h>
                #include <iostream>
                #define WCHAR char
                #define _vsnprintf_s vsnprintf
                #ifdef __ANDROID__
                        #include <android/log.h>
                        #define OutputDebugStringA(msg) __android_log_print(ANDROID_LOG_INFO,"Orkige",msg)
                #else
                        #define OutputDebugStringA(msg) (std::clog << msg)
                #endif
                #define _ASSERTE assert

                #ifndef _cdecl
            #define _cdecl
        #endif
        #else //windows
                #include <crtdbg.h>
                #include <windows.h>
        #endif
        #include <stdarg.h>
        #include <stdio.h>
        #include <sstream>



        #define	oAssert	_ASSERTE

        #ifndef NOTRACE
#ifndef ORKIGE_EXTERN_LOG
                static inline void __orkige_debug_msg(std::string const & msg)
                {
                        OutputDebugStringA(msg.c_str());
                }
#else
                extern void __orkige_debug_msg(std::string const & msg);
#endif
                static inline void _orkige_trace_msg(std::string const & msg, const char * file, int line)
                {
                        std::stringstream out;
                        if(msg.length() > 16384)
                        {
                                __orkige_debug_msg("!!! Trace Message too long !!!\n");
                                out << file << "(" << line << "): " << msg.substr(0,16384) <<std::endl;
                        }
                        else
                        {
                                out << file << "(" << line << "): " <<msg<<std::endl;
                        }
                        __orkige_debug_msg(out.str());
                }



        #else

        #endif

        #ifdef	ORKIGE_DEBUG
                #define oTrace _orkige_trace_msg
                #define	oVerify(f)			oAssert(f)
                #define	oPopup(f)			oInfo(f);oAssert(!f)
                #define	oPopupTrace(msg,trc)	oInfo(msg);oTrace(trc);oAssert(!msg)
        #else	// !_DEBUG
                #define oTrace(...) ((void)0)	//!< args are NOT evaluated in non-debug builds
                #define oPopup(msg)			((void)(msg))
                #define oPopupTrace(msg,trc)	((void)(msg))
                #define	oVerify(f)			((void)(f))
        #endif	// _DEBUG

#endif	// _MFC_VER

#ifdef ORKIGE_DEBUG
        static std::stringstream _orkige_trace_msg_stringstream;
        #define	oDebugCode(f)			(f)
        #define	oDebugBreak()			DebugBreak()
        #define oInfo(msg)				_orkige_trace_msg_stringstream.str("");\
                                                                        _orkige_trace_msg_stringstream << msg;\
                                                                        oTrace(_orkige_trace_msg_stringstream.str(),__FILE__,__LINE__)

#else	// !_DEBUG
        #define	oDebugCode(f)
        #define	oDebugBreak()
        #define oInfo(f)
#endif	// _DEBUG

inline bool __isNull(const void * pointer, const char* file, int line)
{
        if(pointer==NULL) {
                #ifdef ORKIGE_DEBUG
                        oTrace("NULLPOINTER!\n", file, line);
                #endif //_DEBUG
                return true;
        }else {
                return false;
        }
}

#define oIsNull(expr) __isNull(expr, __FILE__, __LINE__)

//! @brief the tagged, level-gated diagnostic macros - ALWAYS compiled, runtime
//! gated through the log table (per-tag threshold, settable live via the log.<tag>
//! cvars). Stream-style: the message expression is an ostream chain and is NOT
//! evaluated when the tag is silent at that level, so a disabled call is cheap.
//! The legacy second `level` argument is accepted for source compatibility and no
//! longer used - the severity is fixed by the macro (Msg=debug, Warning=warn,
//! Error=error). An oDebugError also lands in the crash breadcrumbs.
#define ORKIGE_LOG_EMIT(tag, lvl, message)							\
	do {															\
		if(::Orkige::logTagEnabled((tag), (lvl)))					\
		{															\
			std::ostringstream _orkigeLogStream;					\
			_orkigeLogStream << message;							\
			::Orkige::logEmit((tag), (lvl), _orkigeLogStream.str(),	\
				__FILE__, __LINE__);								\
		}															\
	} while(0)

#define oDebugMsg(tag, level, message)		ORKIGE_LOG_EMIT((tag), ::Orkige::LL_DEBUG, message)
//! tagged WARN sibling of oDebugMsg/oDebugError (the unconditional counterpart
//! to the condition-guarded oDebugWarning below): a recoverable anomaly the
//! user should see. On by default, so it reaches the editor Console.
#define oDebugWarn(tag, level, message)		ORKIGE_LOG_EMIT((tag), ::Orkige::LL_WARN, message)
#define oDebugError(tag, level, message)	ORKIGE_LOG_EMIT((tag), ::Orkige::LL_ERROR, message)
#define oDebugWrite(tag, level, message, priority, file, line)	ORKIGE_LOG_EMIT((tag), ::Orkige::LL_DEBUG, message)

//! condition-guarded warning: emits (at warn level, tag "engine") only when the
//! condition is false. The message stream is untouched while the condition holds.
#define oDebugWarning(condition, message)							\
	do {															\
		if(!(condition))											\
		{															\
			ORKIGE_LOG_EMIT("engine", ::Orkige::LL_WARN, message);	\
		}															\
	} while(0)

//! non-tagged severities (plain string message), routed through the same table
//! under the "engine" tag. Kept for source compatibility.
#define oNotify(message)	ORKIGE_LOG_EMIT("engine", ::Orkige::LL_INFO, message)
#define oWarning(message)	ORKIGE_LOG_EMIT("engine", ::Orkige::LL_WARN, message)
#define oError(message)		ORKIGE_LOG_EMIT("engine", ::Orkige::LL_ERROR, message)

#define oAssertDesc(condition, message)	oDebugWarning(condition, message); oAssert(condition);

#endif //__DebugMacros_h__8_9_2010__10_15_05__
