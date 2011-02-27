/**************************************************************
	created:	2010/09/08 at 10:15
	filename: 	DebugMacros.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec

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
#include <sstream>
//#undef _DEBUG //test defines

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
		#ifndef ORKIGE_NDS
			#define OutputDebugStringA(msg) (std::clog << msg)
			#define _ASSERTE assert
		#else
			#define OutputDebugStringA(msg) OS_Printf(msg)
			#define _ASSERTE SDK_ASSERT
		#endif	
		
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
		#define oTrace (void)
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

inline bool __isNull(const void * pointer, char* file, int line)
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

#define oNotify(message) ::Orkige::LogManager::write(message, ::Orkige::LogManager::LOGNOTIFY, __FILE__, __LINE__)
#define oWarning(message) ::Orkige::LogManager::write(message, ::Orkige::LogManager::LOGWARNING, __FILE__, __LINE__)
#define oError(message) ::Orkige::LogManager::write(message, ::Orkige::LogManager::LOGERROR, __FILE__, __LINE__)


#ifdef ORKIGE_DEBUG
	static std::stringstream _orkige_debug_msg_stringstream;
	
#	define oDebugWrite(tag, level, message, priority, file, line)	::Orkige::LogManager::getSingleton().write(tag,level,message, priority, file, line)

#	define oDebugMsg(tag, level, message)							_orkige_debug_msg_stringstream.str("");\
																	_orkige_debug_msg_stringstream << message; \
																	oDebugWrite(tag,level,_orkige_debug_msg_stringstream.str(), ::Orkige::LogManager::LOGDEBUG, __FILE__, __LINE__)

#	define oDebugWarning(condition, message)						if(!(condition))\
																	{\
																		_orkige_debug_msg_stringstream.str("");\
																		_orkige_debug_msg_stringstream << message;\
																		::Orkige::LogManager::getSingleton().write(_orkige_debug_msg_stringstream.str(), ::Orkige::LogManager::LOGWARNING, __FILE__, __LINE__);\
																	}

#	define oDebugError(tag, level, message)							oDebugWrite(tag,level,message, ::Orkige::LogManager::LOGERROR, __FILE__, __LINE__)
#else
#	define oDebugWrite(tag, level, message, file, line)
#	define oDebugMsg(tag, level, message)
#	define oDebugWarning(condition, message)
#	define oDebugError(tag, level, message)
#endif
#define oAssertDesc(condition, message)	oDebugWarning(condition, message); oAssert(condition);

#endif //__DebugMacros_h__8_9_2010__10_15_05__
