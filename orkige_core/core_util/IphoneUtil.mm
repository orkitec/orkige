/**************************************************************
	created:	2010/08/19 at 23:21
	filename: 	IphoneUtil.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
***************************************************************/
#include "core_util/IphoneUtil.h"
#ifdef __APPLE__
#import <Foundation/NSString.h>
#import <Foundation/NSPathUtilities.h>
#import <Foundation/NSBundle.h>
#endif

namespace Orkige
{
	namespace IPhoneUtil
	{
#ifdef __APPLE__
		String GetIPhoneDataPath()
		{
			NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
			char *path  = (char*)[bundlePath cStringUsingEncoding:1];
			strcat(path,"/");
			return path;
		}
		//---------------------------------------------------------
		String GetIPhoneDocumentsDirectory()
		{
			String str;
			str = [[NSSearchPathForDirectoriesInDomains( NSDocumentDirectory
				, NSUserDomainMask
				, YES) objectAtIndex:0] UTF8String];
			str += "/";
			return str;
		}
		//---------------------------------------------------------
#endif
	}
}
