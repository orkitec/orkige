/********************************************************************
	created:	Tuesday 2010/08/10 at 15:56
	filename: 	StringUtil.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec	
*********************************************************************/
#ifndef __StringUtil_h__10_8_2010__15_56_23__
#define __StringUtil_h__10_8_2010__15_56_23__

#include "core_debug/MemoryManager.h"
#include "core_util/String.h"
#include "core_base/Value.h"

namespace Orkige
{
	//! String Utilities
	namespace StringUtil
	{
		//! wrapper around String to make it a Orkige::Object
		typedef Value<String> StringObject;
		//! blank String definition
		static String BLANK = "";

		//! @return true if strings match :)
		bool StringCompare(const char  *s1, const char  *s2);

		//! @return true if str is "true","TRUE","True" or "1"
		//! @return also false if char is NULLPOINTER
		bool StringToBool(const char * str);

		//! @return value of str as int and 0 if str is a NULLPOINTER
		int StringToInt(const char * str);

		//! @return i as String
		String intToString(int i);

		//! @return float val as String
		String floatToString(float val, unsigned short precision = 6, unsigned short width = 0, char fill = ' ', std::ios::fmtflags flags = std::ios::fmtflags(0) );
		
		//! @return double val as String
		String doubleToString(double val, unsigned short precision = 6, unsigned short width = 0, char fill = ' ', std::ios::fmtflags flags = std::ios::fmtflags(0) );


#ifdef ORKIGE_NDS
		//! convert in String to lowercase
		inline void to_lower(String & in)
		{
			for(int i = 0; i < in.length(); i++)
			{
				in[i] = tolower(in[i]);
			}
		}
		//! @return in String copy in lowercase
		inline String to_lower_copy(String const & in)
		{
			String copy = in;
			to_lower(copy);
			return copy;
		}
#else
		//! convert in String to lowercase
		using boost::to_lower;
		//! @return in String copy in lowercase
		using boost::to_lower_copy;
#endif
	};
	//---------------------------------------------------------------
}

#endif //__StringUtil_h__10_8_2010__15_56_23__



