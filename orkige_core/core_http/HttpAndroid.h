/**************************************************************
	created:	2026/07/30 at 16:00
	filename: 	HttpAndroid.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __HttpAndroid_h__30_7_2026__16_00_00__
#define __HttpAndroid_h__30_7_2026__16_00_00__

#include "core_module/OrkigePrerequisites.h"

namespace Orkige
{
	/** \addtogroup Http
	*  @{ */

	//! @brief the app-owned Java VM the Android HTTP transport talks to the
	//! platform's own HTTP stack through: the app registers the process's
	//! JavaVM* here once it exists, and the transport takes the handle instead
	//! of a dependency on whatever created it (the PlatformWindow registration
	//! pattern, one layer down).
	//!
	//! WHY a registration seam rather than a JNI_OnLoad of our own: the window
	//! toolkit already defines the process's JNI entry point, and orkige_core is
	//! deliberately free of any toolkit dependency. Handing the VM in keeps the
	//! handle explicit, testable and honest - a host that registers nothing gets
	//! a transport that refuses with a reason instead of one that crashes.
	//!
	//! The pointer is a JavaVM* passed as void* so no JNI header reaches the
	//! widely-included core prerequisites; the ONE translation unit that
	//! actually speaks JNI casts it back. Every function here is a no-op
	//! accessor on every other platform, so call sites need no platform guard
	//! beyond the one that has a VM to offer in the first place.
	namespace HttpAndroid
	{
		//! register the process's JavaVM* (opaque here); NULL detaches
		ORKIGE_CORE_DLL void setJavaVM(void * javaVm);
		//! the registered JavaVM* (NULL when the host registered none)
		ORKIGE_CORE_DLL void * getJavaVM();
	}

	/** @} */
}

#endif //__HttpAndroid_h__30_7_2026__16_00_00__
