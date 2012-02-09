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
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_same.hpp>
#include <core_util/StringUtil.h>
#include "engine_util/StringConverter.h"
#include <OgreUTFString.h>

namespace Orkige
{
	namespace StringUtil
	{
		//! convert given String to Ogre::UTFString
		static inline Ogre::UTFString convertToUTF(String const & text)
		{
			Ogre::UTFString utfString;
			Ogre::UTFString::code_point cp;
			for (std::size_t i = 0; i < text.size(); ++i)
			{
				cp = text[i];
				cp &= 0xFF;
				utfString.append(1, cp);
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
