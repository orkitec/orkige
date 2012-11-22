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
			NSString* bundleID = [NSString stringWithCString:applicationName.c_str() encoding:[NSString defaultCStringEncoding]];//[[NSBundle mainBundle] bundleIdentifier];
			NSFileManager* fm = [NSFileManager defaultManager];
			NSMutableString* applicationSupportFolder = nil;

			FSRef foundRef;
			OSErr err = FSFindFolder(kUserDomain, kApplicationSupportFolderType,
									 kDontCreateFolder, &foundRef);
			if (err != noErr)
			{
				NSLog(@"Can't find application support folder");
			}
			else
			{
				unsigned char path[1024];
				FSRefMakePath(&foundRef, path, sizeof(path));
				applicationSupportFolder = [NSString stringWithUTF8String:(char *)path];

			}


			// Append the bundle ID to the URL for the
			// Application Support directory		
			// This did not work: 
			//[applicationSupportFolder appendString:bundleID];
			// That's why I did this: (pe)
			String tempString = String([applicationSupportFolder UTF8String] + String("/") + applicationName + String("/"));
			applicationSupportFolder = [NSString stringWithCString:tempString.c_str() encoding:[NSString defaultCStringEncoding]];

			bool dirExisits = [fm fileExistsAtPath: applicationSupportFolder];
			if (!dirExisits )
			{
				// If the directory does not exist, this method creates it.
				NSError* theError = nil;
				if (![fm createDirectoryAtPath:applicationSupportFolder attributes:nil ])
				{
					// Handle the error.
					NSLog(@"Could not create Application Support folder: /@", applicationSupportFolder);
					//NSAssert(0, @"Could not create Application Support folder");
					return nil;
				}
			}
			
			static String path = String([applicationSupportFolder UTF8String]);			
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
