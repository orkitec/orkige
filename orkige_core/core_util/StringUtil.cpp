/********************************************************************
	created:	Tuesday 2010/08/10 at 15:58
	filename: 	StringUtil.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2010 orkitec
*********************************************************************/

#include "core_util/StringUtil.h"
#include "core_debug/DebugMacros.h"
#include <iostream>
#include <sstream>
#include <stdlib.h>
#include <string.h>

namespace Orkige
{
	namespace StringUtil
	{
		bool StringCompare(const char *s1, const char *s2)
		{
			if (strcmp(s1, s2))
				return false;

			return true;
		}
		//---------------------------------------------------------------
		bool StringToBool(const char* str)
		{
			if(oIsNull(str))
				return false;

			if(StringCompare(str,"true") || StringCompare(str,"TRUE")|| StringCompare(str,"True") || StringCompare(str,"1"))
				return true;

			return false;
		}
		//---------------------------------------------------------------
		int StringToInt(const char* str)
		{
			if(oIsNull(str))
				return 0;

			return atoi(str);
		}
		//---------------------------------------------------------------
		const char* IntToString(int i)
		{
			std::ostringstream temp;
			temp << i;
			return temp.str().c_str();
		}
		//---------------------------------------------------------------
		String floatToString(float val, unsigned short precision, 
			unsigned short width, char fill, std::ios::fmtflags flags)
		{
			return doubleToString((double)val, precision, width, flags);
		}
		//---------------------------------------------------------------
		String doubleToString(double val, unsigned short precision, 
			unsigned short width, char fill, std::ios::fmtflags flags)
		{
			std::stringstream stream;
			stream.precision(precision);
			stream.width(width);
			stream.fill(fill);
			if (flags)
				stream.setf(flags);
			stream << val;
			return stream.str();
		}
	}
}
