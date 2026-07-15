/********************************************************************
	created:	Tuesday 2010/08/10 at 15:56
	filename: 	StringUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec	
*********************************************************************/
#ifndef __StringUtil_h__10_8_2010__15_56_23__
#define __StringUtil_h__10_8_2010__15_56_23__

#include "core_util/String.h"
#include "core_base/Value.h"
#include <algorithm>

namespace Orkige
{
	//! String Utilities
	namespace StringUtil
	{
		//! wrapper around String to make it a Orkige::Object
		typedef Value<String> StringObject;
		//! blank String definition
		static const String BLANK = "";

		//! @return true if strings match :)
		bool ORKIGE_CORE_DLL charStringCompare(const char  *s1, const char  *s2);

		//! @return true if str is "true","TRUE","True" or "1"
		//! @return also false if char is NULLPOINTER
		bool ORKIGE_CORE_DLL charStringToBool(const char * str);

		//! @return value of str as int and 0 if str is a NULLPOINTER
		int ORKIGE_CORE_DLL charStringToInt(const char * str);

		//! @return i as String
		String ORKIGE_CORE_DLL intToString(int i);

		//! @return float val as String
		String ORKIGE_CORE_DLL floatToString(float val, unsigned short precision = 6, unsigned short width = 0, char fill = ' ', std::ios::fmtflags flags = std::ios::fmtflags(0) );
		
		//! @return double val as String
		String ORKIGE_CORE_DLL doubleToString(double val, unsigned short precision = 6, unsigned short width = 0, char fill = ' ', std::ios::fmtflags flags = std::ios::fmtflags(0) );

		//! converts a utf-8 multi byte char to a wide char
		std::size_t ORKIGE_CORE_DLL multibyteCharStringToWideCharString(wchar_t* wideCharString, const char* multiByteCharString, std::size_t searchLength);
		//! convert in String to lowercase
		inline void to_lower(String & in)
		{
			std::transform(in.begin(), in.end(), in.begin(), [](unsigned char c) { return static_cast<char>(::tolower(c)); });
		}
		//! @return in String copy in lowercase
		inline String to_lower_copy(String const & in)
		{
			String copy = in;
			to_lower(copy);
			return copy;
		}
	};
	//---------------------------------------------------------------
}

#endif //__StringUtil_h__10_8_2010__15_56_23__



