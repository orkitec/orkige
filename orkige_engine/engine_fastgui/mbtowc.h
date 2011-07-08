/**************************************************************
 created:	2011/07/07
 filename: 	mbtowc.h
 author:	philipp.engelhard
 purpose:	converts a utf-8 multi byte char to a wide char
 copyright:	(c) 2011 kunst-stoff
 ***************************************************************/

#ifndef __mbtowc__h__
#define __mbtowc__h__


size_t multiByteToWchar (wchar_t* unicodeChar, const char* inString, size_t searchLength)
{
	if (inString == NULL)
	{
		return 0;
	}
	else
	{
		const char* start = inString;
		if (searchLength == 0)
		{
			return (size_t)(-2);
		}
		
		unsigned char currentChar;
		currentChar = (unsigned char) *inString;
		if (currentChar < 0x80)
		{
			if (unicodeChar != NULL)
			{
				*unicodeChar = (wchar_t) currentChar;
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
			size_t count;
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
				return (size_t)(-2);
			}
			
			inString++;
			currentChar = (unsigned char) *inString++ ^ 0x80;
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
					currentChar = (unsigned char) *inString++ ^ 0x80;
					if (!(currentChar < 0x40))
					{
						return 0;
					}
					wideChar |= (wchar_t) currentChar << (6 * --count);
					
				} while (count > 0);
			}
			
			if (unicodeChar != NULL)
			{
				*unicodeChar = wideChar;
			}
			
			return inString - start;
		}
		
    }
}

#endif //__mbtowc__h__