/**************************************************************
	created:	2010/08/19 at 23:21
	filename: 	PlatformUtil.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#include "core_util/PlatformUtil.h"
#ifdef WIN32
#	include <windows.h>
#	include <stdarg.h>
#	include <Shlobj.h>
#endif

namespace Orkige
{
	namespace PlatformUtil
	{
#ifdef WIN32
		String const & getBaseDirectory()
		{
			static String path = "./";
			return path;
		}
		//---------------------------------------------------------
		String const & getDocumentsDirectory()
		{
			static String path;
			if(path.empty())
			{
				TCHAR szPath[MAX_PATH];

				//@Note maybe use CSIDL_APPDATA instead of CSIDL_PERSONAL
				if( SUCCEEDED(SHGetFolderPath(NULL,CSIDL_PERSONAL, NULL, 0, szPath))) 
				{
					path = szPath;
					path += "\\";
				}
			}
			return path;
		}
		//---------------------------------------------------------
		String const & getResourceDirectory()
		{
			static String path = "./";
			return path;
		}
		//---------------------------------------------------------
#endif
	}
}
