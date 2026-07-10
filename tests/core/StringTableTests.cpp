/**************************************************************
	created:	2026/07/10 at 12:00
	filename: 	StringTableTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless localisation unit tests: StringTable get / miss-fallback /
	%%0%% formatting / language switching, plus the sectioned-file parser.
	Backend-neutral (no Ogre, no renderer).
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/StringTable.h"

#include <cstdio>
#include <fstream>
#include <string>

using namespace Orkige;

TEST_CASE("StringTable resolves a key in the active language", "[unit][loc]")
{
	StringTable table;
	table.set("en", "title.name", "Roller");
	table.setLanguage("en");
	CHECK(table.get("title.name") == "Roller");
	CHECK(table.has("title.name"));
}

TEST_CASE("StringTable returns the key itself on a miss", "[unit][loc]")
{
	StringTable table;
	table.set("en", "hud.wins", "WINS");
	table.setLanguage("en");
	CHECK(table.get("no.such.key") == "no.such.key");
	CHECK_FALSE(table.has("no.such.key"));
}

TEST_CASE("StringTable %%0%% formatting substitutes positional args",
	"[unit][loc]")
{
	StringTable table;
	table.set("en", "hud.score", "SCORE: %%0%% / %%1%%");
	table.setLanguage("en");
	StringVector args;
	args.push_back("7");
	args.push_back("10");
	CHECK(table.format("hud.score", args) == "SCORE: 7 / 10");
}

TEST_CASE("StringTable formatting leaves out-of-range placeholders untouched",
	"[unit][loc]")
{
	StringTable table;
	table.set("en", "k", "%%0%% and %%1%%");
	table.setLanguage("en");
	StringVector args;
	args.push_back("only");
	CHECK(table.format("k", args) == "only and %%1%%");
}

TEST_CASE("StringTable switches language", "[unit][loc]")
{
	StringTable table;
	table.set("en", "greet", "Hello");
	table.set("de", "greet", "Hallo");
	table.setLanguage("en");
	CHECK(table.get("greet") == "Hello");
	table.setLanguage("de");
	CHECK(table.get("greet") == "Hallo");
	CHECK(table.hasLanguage("de"));
	CHECK_FALSE(table.hasLanguage("fr"));
}

TEST_CASE("StringTable parses a sectioned multi-language file", "[unit][loc]")
{
	// write a small localisation file with [lang] section headers
	const std::string path =
		std::string(std::tmpnam(nullptr)) + "_orkige_loc.txt";
	{
		std::ofstream file(path.c_str());
		file << "# comment line\n";
		file << "[en]\n";
		file << "title.name = Roller\n";
		file << "hud.wins = WINS: %%0%%\n";
		file << "\n";
		file << "[de]\n";
		file << "title.name = Roller\n";
		file << "hud.wins = SIEGE: %%0%%\n";
	}
	StringTable table;
	REQUIRE(table.loadFile(path));
	// first section becomes active
	CHECK(table.getLanguage() == "en");
	CHECK(table.get("title.name") == "Roller");
	StringVector args;
	args.push_back("3");
	CHECK(table.format("hud.wins", args) == "WINS: 3");
	table.setLanguage("de");
	CHECK(table.format("hud.wins", args) == "SIEGE: 3");
	std::remove(path.c_str());
}

TEST_CASE("StringTable loadFile returns false for a missing file",
	"[unit][loc]")
{
	StringTable table;
	CHECK_FALSE(table.loadFile("/no/such/orkige/loc/file.txt"));
}
