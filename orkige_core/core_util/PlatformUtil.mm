/**************************************************************
	created:	2010/08/19 at 23:21
	filename: 	PlatformUtil.mm
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "core_util/PlatformUtil.h"
#ifdef __APPLE__
#import <Foundation/Foundation.h>
#ifdef ORKIGE_IPHONE
#import <UIKit/UIKit.h>
#endif //ORKIGE_IPHONE
#endif
#include "core_util/StringUtil.h"

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
		String const & getSupportDirectory(String applicationName)
		{
#ifndef ORKIGE_IPHONE
			// modernized 2026: NSSearchPathForDirectoriesInDomains replaces
			// the deprecated Carbon FSFindFolder; directory creation goes
			// through createDirectoryAtPath:withIntermediateDirectories:.
			// Failures return StringUtil::BLANK (the historical `return nil;`
			// from a String const & function was undefined behavior).
			static String path;
			if (!path.empty())
			{
				return path;
			}
			NSArray* folders = NSSearchPathForDirectoriesInDomains(
				NSApplicationSupportDirectory, NSUserDomainMask, YES);
			if ([folders count] == 0)
			{
				NSLog(@"Can't find application support folder");
				return Orkige::StringUtil::BLANK;
			}
			// append the application name to the Application Support directory
			const String supportPath =
				String([[folders objectAtIndex:0] UTF8String]) +
				"/" + applicationName + "/";
			NSString* applicationSupportFolder =
				[NSString stringWithUTF8String:supportPath.c_str()];

			NSFileManager* fm = [NSFileManager defaultManager];
			if (![fm fileExistsAtPath:applicationSupportFolder])
			{
				// If the directory does not exist, this method creates it.
				NSError* theError = nil;
				if (![fm createDirectoryAtPath:applicationSupportFolder
						withIntermediateDirectories:YES
						attributes:nil
						error:&theError])
				{
					NSLog(@"Could not create Application Support folder: %@",
						applicationSupportFolder);
					return Orkige::StringUtil::BLANK;
				}
			}

			path = supportPath;
			return path;
#else //ORKIGE_IPHONE
			return Orkige::StringUtil::BLANK;
#endif //ORKIGE_IPHONE
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
