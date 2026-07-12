/********************************************************************
	created:	Saturday 2026/07/12 at 12:00
	filename: 	LocaleMatch.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/

#include "core_util/LocaleMatch.h"

#include <cctype>

namespace Orkige
{
	namespace
	{
		//! a lowercased copy so the match ignores case (BCP-47 tags are
		//! case-insensitive: "de-DE" and "de-de" name the same locale)
		String lower(String const & text)
		{
			String out(text);
			for(char & character : out)
			{
				character = static_cast<char>(
					std::tolower(static_cast<unsigned char>(character)));
			}
			return out;
		}
		//! the primary language subtag: the run before the first '-' or '_'
		//! separator (both accepted so an underscore-styled tag still splits)
		String primarySubtag(String const & tag)
		{
			const String::size_type dash = tag.find_first_of("-_");
			return dash == String::npos ? tag : tag.substr(0, dash);
		}
	}
	//---------------------------------------------------------
	String pickBestLanguage(StringVector const & available,
		StringVector const & preferred, String const & sourceLanguage)
	{
		// pass one: an EXACT tag match, honoring preference order (a
		// region-qualified table wins when the device asked for that region)
		for(String const & want : preferred)
		{
			const String wantLower = lower(want);
			for(String const & have : available)
			{
				if(lower(have) == wantLower)
				{
					return have;
				}
			}
		}
		// pass two: a primary-subtag match in either direction, so a
		// language-only table serves a region-qualified request and vice versa
		for(String const & want : preferred)
		{
			const String wantPrimary = lower(primarySubtag(want));
			for(String const & have : available)
			{
				if(lower(primarySubtag(have)) == wantPrimary)
				{
					return have;
				}
			}
		}
		// no preference matched: the authored fallback (or "" when absent)
		return sourceLanguage;
	}
}
