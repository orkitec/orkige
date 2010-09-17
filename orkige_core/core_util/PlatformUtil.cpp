/**************************************************************
	created:	2010/08/19 at 23:21
	filename: 	PlatformUtil.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#include "core_util/PlatformUtil.h"

namespace Orkige
{
	namespace PlatformUtil
	{
		String const & getBaseDirectory()
		{
			static String path = "./";
			return path;
		}
		//---------------------------------------------------------
		String const & getDocumentsDirectory()
		{
			static String path = "./";
			return path;
		}
		//---------------------------------------------------------
		String const & getResourceDirectory()
		{
			static String path = "./";
			return path;
		}
		//---------------------------------------------------------
	}
}
