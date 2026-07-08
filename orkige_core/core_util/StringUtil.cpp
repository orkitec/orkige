/********************************************************************
	created:	Tuesday 2010/08/10 at 15:58
	filename: 	StringUtil.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2011 orkitec
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
		bool charStringCompare(const char *s1, const char *s2)
		{
			if (strcmp(s1, s2))
				return false;

			return true;
		}
		//---------------------------------------------------------------
		bool charStringToBool(const char* str)
		{
			if(oIsNull(str))
				return false;

			if(charStringCompare(str,"true") || charStringCompare(str,"TRUE")|| charStringCompare(str,"True") || charStringCompare(str,"1"))
				return true;

			return false;
		}
		//---------------------------------------------------------------
		int charStringToInt(const char* str)
		{
			if(oIsNull(str))
				return 0;

			return atoi(str);
		}
		//---------------------------------------------------------------
		String intToString(int i)
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
		//---------------------------------------------------------------
		std::size_t multibyteCharStringToWideCharString(wchar_t* wideCharString, const char* multiByteCharString, std::size_t searchLength)
		{
			if (multiByteCharString == NULL)
			{
				return 0;
			}
			else
			{
				const char* start = multiByteCharString;
				if (searchLength == 0)
				{
					return (std::size_t)(-2);
				}

				unsigned char currentChar;
				currentChar = (unsigned char) *multiByteCharString;
				if (currentChar < 0x80)
				{
					if (wideCharString != NULL)
					{
						*wideCharString = (wchar_t) currentChar;
					}

					return (currentChar != 0);
				}
				else if (currentChar < 0xC0)
				{
					return 0;
				}
				else
				{
					wchar_t wideChar;
					std::size_t count;
					if (currentChar < 0xE0)
					{
						wideChar = (wchar_t)(currentChar & 0x1F) << 6;
						count = 1;
						if (currentChar < 0xC2)
						{
							return 0;
						}
					} 
					else if (currentChar < 0xF0)
					{
						wideChar = (wchar_t)(currentChar & 0x0F) << 12;
						count = 2;
					}
					else
					{
						return 0;
					}

					if (searchLength <= count)
					{
						return (std::size_t)(-2);
					}

					multiByteCharString++;
					currentChar = (unsigned char) *multiByteCharString++ ^ 0x80;
					if (!(currentChar < 0x40))
					{
						return 0;
					}

					wideChar |= (wchar_t) currentChar << (6 * --count);

					if (count > 0)
					{
						if (wideChar < (1 << (count * 5 + 6)))
						{
							return 0;
						}

						do
						{
							currentChar = (unsigned char) *multiByteCharString++ ^ 0x80;
							if (!(currentChar < 0x40))
							{
								return 0;
							}
							wideChar |= (wchar_t) currentChar << (6 * --count);

						} while (count > 0);
					}

					if (wideCharString != NULL)
					{
						*wideCharString = wideChar;
					}

					return multiByteCharString - start;
				}

			}
		}
	}
}
