/**************************************************************
	created:	2010/08/19 at 23:21
	filename: 	PlatformUtil.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#include "core_util/PlatformUtil.h"
#ifdef __APPLE__
#import <Foundation/NSString.h>
#import <Foundation/NSPathUtilities.h>
#import <Foundation/NSBundle.h>
#ifdef ORKIGE_IPHONE
#import <UIKit/UIKit.h>
#endif //ORKIGE_IPHONE
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
#ifdef ORKIGE_IPHONE
			static String path = getBaseDirectory();
#else
			static String path = getBaseDirectory();
#endif
			return path;
		}
		//---------------------------------------------------------
		const ORKIGE_PLATFORM getPlatform()
		{
#ifdef ORKIGE_IPHONE
#ifdef ORKIGE_IPAD
			return PLATFORM_IPAD;
#else
#ifdef __OBJC__
			if ([[UIScreen mainScreen] respondsToSelector:@selector(scale)] && [[UIScreen mainScreen] scale] == 2.0)
			{
				//>=iphone4
				return PLATFORM_IPHONE4;
			}
			else 
			{
				//older iphones
				return PLATFORM_IPHONE;
			}
#endif
#endif //ORKIGE_IPAD
#else //ORKIGE_IPHONE
			return PLATFORM_MACOS;
#endif //ORKIGE_IPHONE
		}
#endif
	}
}
