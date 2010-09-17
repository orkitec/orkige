/**************************************************************
	created:	2010/08/19 at 23:21
	filename: 	PlatformUtil.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#include "core_util/PlatformUtil.h"
#ifdef __APPLE__
#import <Foundation/NSString.h>
#import <Foundation/NSPathUtilities.h>
#import <Foundation/NSBundle.h>
#endif

namespace Orkige
{
	namespace PlatformUtil
	{
#ifdef __APPLE__
		String const & getBaseDirectory()
		{
			static String path = String((char*)[[[NSBundle mainBundle] bundlePath] cStringUsingEncoding:1]) + "/";
			return path;
		}
		//---------------------------------------------------------
		String const & getDocumentsDirectory()
		{
			static String path = String([[NSSearchPathForDirectoriesInDomains( NSDocumentDirectory, NSUserDomainMask, YES) objectAtIndex:0] UTF8String]) + "/";
			return path;
		}
		//---------------------------------------------------------
		String const & getResourceDirectory()
		{
#ifdef ORKGE_IPHONE
			static String path = getBaseDirectory();
#else
			static String path = getBaseDirectory() + "Contents/Resources/";
#endif
			return path
		}
		//---------------------------------------------------------
#endif
	}
}
