/********************************************************************
	created:	Sunday 2026/08/03 at 12:00
	filename: 	RenderSystemSelection.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#include "engine_render/RenderSystemSelection.h"

#include <cctype>
#include <cstdlib>
#include <string>

namespace Orkige
{
	namespace RenderSystemSelection
	{
		//---------------------------------------------------------
		bool isDevicelessName(String const & name)
		{
			// trim, then lowercase - the env var is typed by hand
			String::size_type begin = 0;
			String::size_type end = name.size();
			while(begin < end && std::isspace(
				static_cast<unsigned char>(name[begin])))
			{
				++begin;
			}
			while(end > begin && std::isspace(
				static_cast<unsigned char>(name[end - 1])))
			{
				--end;
			}
			String word = name.substr(begin, end - begin);
			for(char & each : word)
			{
				each = static_cast<char>(std::tolower(
					static_cast<unsigned char>(each)));
			}
			// "null" is the render system's own name; "headless" is what the
			// word means to a caller. Both, and nothing else - an unknown word
			// is a GRAPHICS name and must not silently turn the display off.
			return word == "null" || word == "headless";
		}
		//---------------------------------------------------------
		bool devicelessRequested()
		{
			const char* name = std::getenv("ORKIGE_RENDERSYSTEM");
			return name != NULL && isDevicelessName(String(name));
		}
		//---------------------------------------------------------
		bool devicelessAvailable()
		{
#ifdef ORKIGE_RENDER_NEXT
			return true;
#else
			return false;
#endif
		}
	}
}
