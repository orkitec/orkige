/**************************************************************
	created:	2026/07/12 at 12:00
	filename: 	LocaleMatchTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless BCP-47 language-pick unit tests: the pure rule the player
	boot uses to turn the device's ordered preferred locales into one of
	the string table's loaded languages (exact tag, then primary-subtag,
	then the source-language fallback). No SDL, no StringTable.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/LocaleMatch.h"

using namespace Orkige;

TEST_CASE("locale pick: exact tag match wins", "[unit][loc]")
{
	const StringVector available = {"de", "de-DE", "en"};
	// the device asks for de-DE first: the exact region table beats "de"
	REQUIRE(pickBestLanguage(available, {"de-DE", "de"}, "en") == "de-DE");
}

TEST_CASE("locale pick: exact match is case-insensitive", "[unit][loc]")
{
	const StringVector available = {"en", "pt-BR"};
	REQUIRE(pickBestLanguage(available, {"PT-br"}, "en") == "pt-BR");
}

TEST_CASE("locale pick: primary-subtag falls region -> language",
	"[unit][loc]")
{
	const StringVector available = {"de", "en"};
	// no de-DE table, but the primary subtag "de" matches the language-only one
	REQUIRE(pickBestLanguage(available, {"de-DE"}, "en") == "de");
}

TEST_CASE("locale pick: primary-subtag falls language -> region",
	"[unit][loc]")
{
	const StringVector available = {"de-AT", "en"};
	// the device asks for bare "de"; the only German table is region-qualified
	REQUIRE(pickBestLanguage(available, {"de"}, "en") == "de-AT");
}

TEST_CASE("locale pick: exact beats a higher-priority primary match",
	"[unit][loc]")
{
	const StringVector available = {"fr", "de"};
	// fr-CA has no exact table but de is exact; the two-pass rule prefers the
	// exact "de" over the primary-only "fr" even though fr-CA was listed first
	REQUIRE(pickBestLanguage(available, {"fr-CA", "de"}, "en") == "de");
}

TEST_CASE("locale pick: preference order breaks ties within a pass",
	"[unit][loc]")
{
	const StringVector available = {"de", "fr"};
	// both are primary-only matches; the first-listed preference wins
	REQUIRE(pickBestLanguage(available, {"fr-FR", "de-DE"}, "en") == "fr");
}

TEST_CASE("locale pick: no match falls back to the source language",
	"[unit][loc]")
{
	const StringVector available = {"en", "de"};
	REQUIRE(pickBestLanguage(available, {"ja", "ko-KR"}, "en") == "en");
}

TEST_CASE("locale pick: no match and no source yields empty", "[unit][loc]")
{
	const StringVector available = {"en", "de"};
	REQUIRE(pickBestLanguage(available, {"ja"}, "").empty());
}

TEST_CASE("locale pick: empty preferred list yields the source", "[unit][loc]")
{
	const StringVector available = {"en", "de"};
	REQUIRE(pickBestLanguage(available, {}, "en") == "en");
}
