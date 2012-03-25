/**************************************************************
	created:	2010/09/07 at 22:21
	filename: 	ResourceUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __ResourceUtil_h__7_9_2010__22_21_27__
#define __ResourceUtil_h__7_9_2010__22_21_27__

#include "engine_module/EnginePrerequisites.h"
#include <core_util/String.h>

namespace Orkige
{
	//! resource utilities
	namespace ResourceUtil
	{
		//! get path for given fileName
		String ORKIGE_ENGINE_DLL findPath(String const & filename);

		void ORKIGE_ENGINE_DLL removeUnusedResources();
	};
	//---------------------------------------------------------
}

#endif //__ResourceUtil_h__7_9_2010__22_21_27__