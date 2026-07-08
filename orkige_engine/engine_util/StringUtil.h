/**************************************************************
	created:	2010/08/31 at 0:38
	filename: 	StringUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
***************************************************************/
#ifndef __StringUtil_h__31_8_2010__0_38_16__
#define __StringUtil_h__31_8_2010__0_38_16__

#include "engine_module/EnginePrerequisites.h"
#include <core_util/StringUtil.h>
#include "engine_util/StringConverter.h"

namespace Orkige
{
	namespace StringUtil
	{
		//! convert given String to Ogre::DisplayString
		//! OGRE 14 dropped Ogre::UTFString; display strings are plain UTF-8 now,
		//! so this encodes every input byte (treated as Latin-1, as the old
		//! UTFString version did) as an UTF-8 code point.
		static inline Ogre::DisplayString convertToUTF(String const & text)
		{
			Ogre::DisplayString utfString;
			for (std::size_t i = 0; i < text.size(); ++i)
			{
				unsigned char cp = static_cast<unsigned char>(text[i]);
				if (cp < 0x80)
				{
					utfString.append(1, static_cast<char>(cp));
				}
				else
				{
					utfString.append(1, static_cast<char>(0xC0 | (cp >> 6)));
					utfString.append(1, static_cast<char>(0x80 | (cp & 0x3F)));
				}
			}
			return utfString;
		}
		//! check if given string has given ending
		static inline bool hasEnding (String const & fullString, String const & ending)
		{
			if (fullString.length() > ending.length()) 
			{
				return (0 == fullString.compare(fullString.length() - ending.length(), ending.length(), ending));
			} 
			else 
			{
				return false;
			}
		}
	}
	//---------------------------------------------------------
}

#endif //__StringUtil_h__31_8_2010__0_38_16__
