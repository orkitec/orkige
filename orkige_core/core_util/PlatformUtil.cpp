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
#ifdef ORKIGE_ENABLE_JADEDS
			static String path = "";
#else
			static String path = "./";
#endif
			return path;
		}
		//---------------------------------------------------------
		String const & getSupportDirectory(String applicationName)
		{
			static String path = getDocumentsDirectory() + applicationName + "";
			//FIXME: get the right path
			return path;
		}
		//---------------------------------------------------------
		const ORKIGE_PLATFORM getPlatform()
		{
			return PLATFORM_WIN32;
		}
		//---------------------------------------------------------
#else
		//for linux
#ifndef __APPLE__
#ifdef __ANDROID__
		//---------------------------------------------------------
		static String _orkige_android_apk_path;
		static String _orkige_android_files_path;
		//---------------------------------------------------------
		void setApkPath(String const & path)
		{
			_orkige_android_apk_path = path;
		}
		//---------------------------------------------------------
		String const & getApkPath()
		{
			return _orkige_android_apk_path;
		}
		//---------------------------------------------------------
		void setFilesPath(String const & path)
		{
			_orkige_android_files_path = path;
		}
		//---------------------------------------------------------
		String const & getSupportDirectory(String applicationName)
		{
			return _orkige_android_files_path;
		}
		//---------------------------------------------------------
		String const & getBaseDirectory()
		{
			return _orkige_android_files_path;
		}
		//---------------------------------------------------------
		String const & getDocumentsDirectory()
		{
			return _orkige_android_files_path;
		}
		//---------------------------------------------------------
		String const & getResourceDirectory()
		{
			return _orkige_android_files_path;
		}
		//---------------------------------------------------------
		const ORKIGE_PLATFORM getPlatform()
		{
			return PLATFORM_ANDROID;
		}
		//---------------------------------------------------------
#else	// LiNUX
		//---------------------------------------------------------
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
		String const & getSupportDirectory(String applicationName)
		{
			static String path = "./";
			return path;
		}
		//---------------------------------------------------------
		const ORKIGE_PLATFORM getPlatform()
		{
			return PLATFORM_LINUX;
		}
		//---------------------------------------------------------
#endif //__ANDROID__
#endif //__APPLE__
#endif
	}
}
