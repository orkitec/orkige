/**************************************************************
	created:	2010/09/17 at 13:25
	filename: 	PlatformUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __PlatformUtil_h__17_9_2010__13_25_23__
#define __PlatformUtil_h__17_9_2010__13_25_23__

#include "core_util/String.h"

namespace Orkige
{
	//! platform specific utilities
	namespace PlatformUtil
	{
		//! retrieve base path of current running app
		String const & getBaseDirectory();
		//! retrieve Documents path
		String const & getDocumentsDirectory();
		//! retrieve Resource path
		String const & getResourceDirectory();
		
		enum ORKIGE_PLATFORM {
			PLATFORM_MACOS,
			PLATFORM_IPHONE,
			PLATFORM_IPHONE4,
			PLATFORM_IPAD,
			PLATFORM_LINUX,
			PLATFORM_WIN32,
		};
		const ORKIGE_PLATFORM getPlatform();
		
	};
	//---------------------------------------------------------
}

#endif //__PlatformUtil_h__17_9_2010__13_25_23__
