/********************************************************************
	created:	Saturday 2026/07/12 at 12:00
	filename: 	LocaleMatch.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __LocaleMatch_h__12_7_2026__12_00_00__
#define __LocaleMatch_h__12_7_2026__12_00_00__

//! @file LocaleMatch.h
//! @brief the pure BCP-47 language-picking rule the player boot uses to turn
//! the device's ordered preferred locales into one of the string table's
//! loaded languages. No SDL, no StringTable, no platform types - the player
//! reads SDL_GetPreferredLocales and StringTable::getLanguages() and feeds both
//! here, and a unit test exercises the rule directly.

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

namespace Orkige
{
	//! @brief the language-matching rule (case-insensitive on the tags):
	//! @remarks Walks the preferred tags in priority order twice. Pass one
	//! returns the first preferred tag that matches an available language
	//! EXACTLY (so "de-DE" beats "de" when both are loaded). Pass two returns
	//! the first preferred tag whose PRIMARY subtag matches an available
	//! language's primary subtag ("de-DE" -> "de", or "de" -> "de-AT"), so a
	//! region-qualified device locale still finds a language-only table and
	//! vice versa. When nothing matches, the source language is returned (the
	//! authored fallback), or "" when even that is absent.
	//!
	//! @param available  every loaded language code (StringTable::getLanguages())
	//! @param preferred  the device's preferred locale tags, most-wanted first
	//! @param sourceLanguage  the fallback when no preference matches
	//! @return the chosen available language code, or sourceLanguage, or ""
	String pickBestLanguage(StringVector const & available,
		StringVector const & preferred, String const & sourceLanguage);
}

#endif //__LocaleMatch_h__12_7_2026__12_00_00__
