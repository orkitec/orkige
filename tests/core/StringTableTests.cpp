/**************************************************************
	created:	2026/07/10 at 12:00
	filename: 	StringTableTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless localisation unit tests: StringTable get / miss-fallback /
	%%0%% formatting / language switching, plus the XLIFF 1.2 loader -
	the usable-state matrix, inline <x/> placeholder round-trip, source-
	then-key fallback ordering, directory scanning and malformed-file
	honesty. Backend-neutral (no Ogre, no renderer).
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "core_util/StringTable.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace Orkige;

namespace
{
	//! a unique temp file path with the requested extension - unique across
	//! PROCESSES, not just within one: ctest runs every test case as its own
	//! process of this binary, so a plain counter restarts at 0 in each and
	//! parallel cases would collide on the same file
	std::filesystem::path uniqueTempPath(const char * extension)
	{
		static const unsigned long long processStamp =
			static_cast<unsigned long long>(
				std::chrono::steady_clock::now().time_since_epoch().count());
		static std::atomic<unsigned int> counter{0};
		const unsigned int n = counter.fetch_add(1);
		return std::filesystem::temp_directory_path() /
			("orkige_loc_" + std::to_string(processStamp) + "_" +
				std::to_string(n) + extension);
	}

	//! write text to a path, returning the path as a string
	std::string writeFile(std::filesystem::path const & path,
		std::string const & content)
	{
		std::ofstream file(path, std::ios::binary);
		file << content;
		file.close();
		return path.string();
	}

	//! a self-contained .xlf whose body is the caller's trans-units
	std::string xliffDoc(const char * sourceLang, const char * targetLang,
		std::string const & body)
	{
		std::string out =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<xliff version=\"1.2\" "
			"xmlns=\"urn:oasis:names:tc:xliff:document:1.2\">\n"
			"  <file original=\"orkige-strings\" datatype=\"plaintext\" "
			"source-language=\"";
		out += sourceLang;
		out += "\"";
		if(targetLang != nullptr)
		{
			out += " target-language=\"";
			out += targetLang;
			out += "\"";
		}
		out += ">\n    <body>\n";
		out += body;
		out += "    </body>\n  </file>\n</xliff>\n";
		return out;
	}

	//! one trans-unit with an optional target (targetXml null => no <target>)
	std::string transUnit(const char * key, std::string const & sourceXml,
		const char * state, const char * targetXml)
	{
		std::string out = "      <trans-unit id=\"";
		out += key;
		out += "\" resname=\"";
		out += key;
		out += "\" xml:space=\"preserve\">\n        <source>";
		out += sourceXml;
		out += "</source>\n";
		if(targetXml != nullptr)
		{
			out += "        <target";
			if(state != nullptr)
			{
				out += " state=\"";
				out += state;
				out += "\"";
			}
			out += ">";
			out += targetXml;
			out += "</target>\n";
		}
		out += "      </trans-unit>\n";
		return out;
	}
}

//--- public-surface behaviour (unchanged by the XLIFF cutover) -------------

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

//--- XLIFF 1.2 loader ------------------------------------------------------

TEST_CASE("StringTable parses a representative XLIFF target file", "[unit][loc]")
{
	std::string body;
	body += transUnit("menu.play", "Play", "translated", "Spielen");
	// inline <x/> in both source and target, with the argument REORDERED in
	// the translation - the id carries the position, so reorder is free
	body += transUnit("hud.score",
		"Score: <x id=\"0\" ctype=\"x-orkige-arg\"/>",
		"translated",
		"<x id=\"0\" ctype=\"x-orkige-arg\"/> Punkte");
	// xml:space="preserve": leading and trailing spaces survive
	body += transUnit("hud.pad", "  padded  ", "translated", "  gefuellt  ");
	const std::string path =
		writeFile(uniqueTempPath(".xlf"), xliffDoc("en", "de", body));

	StringTable table;
	REQUIRE(table.loadXliffFile(path));
	table.setLanguage("de");
	CHECK(table.get("menu.play") == "Spielen");
	// <x id="N"/> reconstructs to %%N%% in memory
	CHECK(table.get("hud.score") == "%%0%% Punkte");
	StringVector args;
	args.push_back("7");
	CHECK(table.format("hud.score", args) == "7 Punkte");
	CHECK(table.get("hud.pad") == "  gefuellt  ");
	// the file keyed on resname/id and established the source language
	CHECK(table.getSourceLanguage() == "en");
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("StringTable target usable-state matrix", "[unit][loc]")
{
	// each unit has a DISTINCT source so a fall-back-to-source is observable;
	// usable states keep the target, the rest fall through to the source text
	struct Row { const char * key; const char * state; const char * target;
		bool usable; };
	const Row rows[] = {
		{ "s.translated",       "translated",              "T", true  },
		{ "s.reviewed",         "reviewed",                "T", true  },
		{ "s.needsRevTrans",    "needs-review-translation","T", true  },
		{ "s.needsRevL10n",     "needs-review-l10n",       "T", true  },
		{ "s.needsRevAdapt",    "needs-review-adaptation", "T", true  },
		{ "s.final",            "final",                   "T", true  },
		{ "s.signedOff",        "signed-off",              "T", true  },
		{ "s.absentState",      nullptr,                   "T", true  },
		{ "s.new",              "new",                     "T", false },
		{ "s.needsTranslation", "needs-translation",       "T", false },
		{ "s.needsAdaptation",  "needs-adaptation",        "T", false },
		{ "s.needsL10n",        "needs-l10n",              "T", false },
		{ "s.unknown",          "banana",                  "T", false },
		{ "s.emptyTarget",      "translated",              "",  false }
	};
	std::string body;
	for(Row const & row : rows)
	{
		std::string source = row.key;	// unique per row
		body += transUnit(row.key, source, row.state, row.target);
	}
	// one row with NO <target> at all
	body += transUnit("s.noTarget", "s.noTarget", nullptr, nullptr);
	const std::string path =
		writeFile(uniqueTempPath(".xlf"), xliffDoc("en", "de", body));

	StringTable table;
	REQUIRE(table.loadXliffFile(path));
	table.setLanguage("de");
	for(Row const & row : rows)
	{
		if(row.usable)
		{
			CHECK(table.get(row.key) == "T");
		}
		else
		{
			// falls back to the unit's own source text (== the key here)
			CHECK(table.get(row.key) == row.key);
		}
	}
	CHECK(table.get("s.noTarget") == "s.noTarget");
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("StringTable resolves active then source then key", "[unit][loc]")
{
	std::string body;
	body += transUnit("has.target", "SourceA", "translated", "ZielA");
	body += transUnit("no.target", "SourceB", nullptr, nullptr);
	const std::string path =
		writeFile(uniqueTempPath(".xlf"), xliffDoc("en", "de", body));

	StringTable table;
	REQUIRE(table.loadXliffFile(path));
	table.setLanguage("de");
	CHECK(table.get("has.target") == "ZielA");	// active language
	CHECK(table.get("no.target") == "SourceB");	// source fallback
	CHECK(table.get("absent.key") == "absent.key");	// key fallback
	// a target file is self-contained: its sources seed the source language
	CHECK(table.hasLanguage("en"));
	table.setLanguage("en");
	CHECK(table.get("has.target") == "SourceA");
	CHECK(table.get("no.target") == "SourceB");
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("StringTable reconstructs reordered inline placeholders",
	"[unit][loc]")
{
	std::string body;
	body += transUnit("re.order",
		"<x id=\"0\" ctype=\"x-orkige-arg\"/> then "
		"<x id=\"1\" ctype=\"x-orkige-arg\"/>",
		"translated",
		"<x id=\"1\" ctype=\"x-orkige-arg\"/> vor "
		"<x id=\"0\" ctype=\"x-orkige-arg\"/>");
	const std::string path =
		writeFile(uniqueTempPath(".xlf"), xliffDoc("en", "de", body));

	StringTable table;
	REQUIRE(table.loadXliffFile(path));
	table.setLanguage("de");
	CHECK(table.get("re.order") == "%%1%% vor %%0%%");
	StringVector args;
	args.push_back("A");
	args.push_back("B");
	CHECK(table.format("re.order", args) == "B vor A");
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("StringTable loads a source-only registry file", "[unit][loc]")
{
	// en.xlf: no target-language, no <target> elements - a valid 1.2 file
	std::string body;
	body += transUnit("menu.play", "Play", nullptr, nullptr);
	body += transUnit("menu.quit", "Quit", nullptr, nullptr);
	const std::string path =
		writeFile(uniqueTempPath(".xlf"), xliffDoc("en", nullptr, body));

	StringTable table;
	REQUIRE(table.loadXliffFile(path));
	CHECK(table.getSourceLanguage() == "en");
	CHECK(table.getLanguage() == "en");	// defaults active to the source
	CHECK(table.get("menu.play") == "Play");
	CHECK(table.get("menu.quit") == "Quit");
	StringVector langs = table.getLanguages();
	REQUIRE(langs.size() == 1);
	CHECK(langs[0] == "en");
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("StringTable loads a directory and skips a disagreeing source",
	"[unit][loc]")
{
	const std::filesystem::path dir = uniqueTempPath("_dir");
	std::filesystem::create_directories(dir);
	// registry (source-only) + one target + one file with a WRONG source lang
	writeFile(dir / "en.xlf",
		xliffDoc("en", nullptr,
			transUnit("greet", "Hello", nullptr, nullptr)));
	writeFile(dir / "de.xlf",
		xliffDoc("en", "de",
			transUnit("greet", "Hello", "translated", "Hallo")));
	writeFile(dir / "xx.xlf",
		xliffDoc("fr", "fr",
			transUnit("greet", "Bonjour", "translated", "Salut")));

	StringTable table;
	REQUIRE(table.loadXliffDirectory(dir.string()));
	// en + de load; the fr-source file is skipped (source language disagrees)
	StringVector langs = table.getLanguages();
	CHECK(table.hasLanguage("en"));
	CHECK(table.hasLanguage("de"));
	CHECK_FALSE(table.hasLanguage("fr"));
	REQUIRE(langs.size() == 2);
	CHECK(langs[0] == "de");	// sorted
	CHECK(langs[1] == "en");
	table.setLanguage("de");
	CHECK(table.get("greet") == "Hallo");
	table.setLanguage("en");
	CHECK(table.get("greet") == "Hello");	// registry source authoritative
	std::error_code ignored;
	std::filesystem::remove_all(dir, ignored);
}

TEST_CASE("StringTable loadXliffDirectory fails on a missing directory",
	"[unit][loc]")
{
	StringTable table;
	CHECK_FALSE(table.loadXliffDirectory("/no/such/orkige/loc/dir"));
}

//--- malformed-file honesty ------------------------------------------------

TEST_CASE("StringTable rejects a truncated file without a partial load",
	"[unit][loc]")
{
	// a good file first, then a broken one - the good data must survive
	const std::string good = writeFile(uniqueTempPath(".xlf"),
		xliffDoc("en", "de",
			transUnit("keep", "Keep", "translated", "Behalten")));
	const std::string broken = writeFile(uniqueTempPath(".xlf"),
		"<?xml version=\"1.0\"?><xliff version=\"1.2\"><file><body>"
		"<trans-unit id=\"x\"><source>oops");	// never closed

	StringTable table;
	REQUIRE(table.loadXliffFile(good));
	CHECK_FALSE(table.loadXliffFile(broken));
	// nothing from the broken file leaked; the good table is intact
	table.setLanguage("de");
	CHECK(table.get("keep") == "Behalten");
	CHECK(table.get("x") == "x");	// the truncated unit never loaded
	std::error_code ignored;
	std::filesystem::remove(good, ignored);
	std::filesystem::remove(broken, ignored);
}

TEST_CASE("StringTable rejects a wrong-root document", "[unit][loc]")
{
	const std::string path = writeFile(uniqueTempPath(".xlf"),
		"<?xml version=\"1.0\"?>\n<notxliff><file><body/></file></notxliff>\n");
	StringTable table;
	CHECK_FALSE(table.loadXliffFile(path));
	CHECK(table.getLanguages().empty());
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("StringTable rejects duplicate keys without a partial load",
	"[unit][loc]")
{
	std::string body;
	body += transUnit("dup", "First", "translated", "Erste");
	body += transUnit("clean", "Clean", "translated", "Sauber");
	body += transUnit("dup", "Second", "translated", "Zweite");
	const std::string path =
		writeFile(uniqueTempPath(".xlf"), xliffDoc("en", "de", body));

	StringTable table;
	CHECK_FALSE(table.loadXliffFile(path));
	// the whole file was rejected - not even the clean unit committed
	CHECK(table.getLanguages().empty());
	CHECK_FALSE(table.hasLanguage("de"));
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("StringTable miss falls back consistently across repeated lookups",
	"[unit][loc]")
{
	// the once-per-key miss log is a side effect; the observable contract is
	// that repeated lookups of a fallen-through key stay stable and correct
	std::string body;
	body += transUnit("only.source", "OnlySource", nullptr, nullptr);
	const std::string path =
		writeFile(uniqueTempPath(".xlf"), xliffDoc("en", "de", body));

	StringTable table;
	REQUIRE(table.loadXliffFile(path));
	table.setLanguage("de");
	for(int i = 0; i < 5; ++i)
	{
		CHECK(table.get("only.source") == "OnlySource");	// source fallback
		CHECK(table.get("never.defined") == "never.defined");	// key fallback
	}
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

TEST_CASE("StringTable clear drops languages and source", "[unit][loc]")
{
	const std::string path = writeFile(uniqueTempPath(".xlf"),
		xliffDoc("en", "de",
			transUnit("k", "V", "translated", "W")));
	StringTable table;
	REQUIRE(table.loadXliffFile(path));
	CHECK_FALSE(table.getLanguages().empty());
	table.clear();
	CHECK(table.getLanguages().empty());
	CHECK(table.getLanguage().empty());
	CHECK(table.getSourceLanguage().empty());
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}
