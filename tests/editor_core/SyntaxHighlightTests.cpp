/**************************************************************
	created:	2026/07/14 at 10:00
	filename: 	SyntaxHighlightTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the Inspector text-preview highlighter
	(tools/editor/SyntaxHighlight.{h,cpp}): the three grammar
	families (Lua / IniLike / Markup), the per-line span invariants
	(ordered, gap-free, full cover), pathological line lengths and
	the extension -> format mapping.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <SyntaxHighlight.h>

#include <string>
#include <vector>

using namespace Orkige;

namespace
{
	//! the sub-string a span covers within `line`
	std::string spanText(std::string const& line, SyntaxSpan const& s)
	{
		return line.substr(s.begin, s.end - s.begin);
	}

	//! find the FIRST span of a given token class (nullptr when absent)
	SyntaxSpan const* firstOf(std::vector<SyntaxSpan> const& spans, SyntaxToken t)
	{
		for (SyntaxSpan const& s : spans)
		{
			if (s.token == t)
			{
				return &s;
			}
		}
		return nullptr;
	}

	//! the spans cover [0, n) exactly: ordered, non-overlapping, gap-free
	void requireGapFreeCover(std::vector<SyntaxSpan> const& spans, std::size_t n)
	{
		if (n == 0)
		{
			REQUIRE(spans.empty());
			return;
		}
		REQUIRE_FALSE(spans.empty());
		REQUIRE(spans.front().begin == 0);
		REQUIRE(spans.back().end == n);
		for (std::size_t i = 0; i < spans.size(); ++i)
		{
			CHECK(spans[i].begin < spans[i].end);	// non-empty
			if (i > 0)
			{
				CHECK(spans[i].begin == spans[i - 1].end);	// contiguous
			}
		}
	}
}

TEST_CASE("highlightLine covers every family gap-free", "[syntax]")
{
	for (SyntaxFormat fmt : { SyntaxFormat::PlainText, SyntaxFormat::Lua,
		SyntaxFormat::IniLike, SyntaxFormat::Markup })
	{
		const std::string line = "local x = \"hi\" -- 42 [Section] <tag> @key";
		const std::vector<SyntaxSpan> spans = highlightLine(line, fmt);
		requireGapFreeCover(spans, line.size());
	}
}

TEST_CASE("an empty line yields no spans", "[syntax]")
{
	CHECK(highlightLine("", SyntaxFormat::Lua).empty());
	CHECK(highlightLine("", SyntaxFormat::Markup).empty());
}

TEST_CASE("PlainText is a single Text span", "[syntax]")
{
	const std::string line = "just some prose 123 \"quoted\"";
	const std::vector<SyntaxSpan> spans = highlightLine(line, SyntaxFormat::PlainText);
	REQUIRE(spans.size() == 1);
	CHECK(spans[0].token == SyntaxToken::Text);
	CHECK(spans[0].begin == 0);
	CHECK(spans[0].end == line.size());
}

//--- Lua --------------------------------------------------------------------

TEST_CASE("Lua highlights a line comment to end of line", "[syntax]")
{
	const std::string line = "x = 1 -- set the count";
	const std::vector<SyntaxSpan> spans = highlightLine(line, SyntaxFormat::Lua);
	requireGapFreeCover(spans, line.size());
	SyntaxSpan const* comment = firstOf(spans, SyntaxToken::Comment);
	REQUIRE(comment != nullptr);
	CHECK(spanText(line, *comment) == "-- set the count");
	CHECK(comment->end == line.size());
}

TEST_CASE("Lua highlights strings, numbers and keywords", "[syntax]")
{
	const std::string line = "local name = \"orkige\" and 3.14";
	const std::vector<SyntaxSpan> spans = highlightLine(line, SyntaxFormat::Lua);
	requireGapFreeCover(spans, line.size());

	SyntaxSpan const* str = firstOf(spans, SyntaxToken::String);
	REQUIRE(str != nullptr);
	CHECK(spanText(line, *str) == "\"orkige\"");

	SyntaxSpan const* num = firstOf(spans, SyntaxToken::Number);
	REQUIRE(num != nullptr);
	CHECK(spanText(line, *num) == "3.14");

	SyntaxSpan const* kw = firstOf(spans, SyntaxToken::Keyword);
	REQUIRE(kw != nullptr);
	CHECK(spanText(line, *kw) == "local");	// the FIRST keyword

	// a keyword substring inside an identifier is NOT highlighted ("android"
	// contains "and", "andy" starts with it)
	const std::string ident = "android = andy";
	const std::vector<SyntaxSpan> identSpans =
		highlightLine(ident, SyntaxFormat::Lua);
	CHECK(firstOf(identSpans, SyntaxToken::Keyword) == nullptr);
}

TEST_CASE("Lua handles a hex literal and an unterminated string", "[syntax]")
{
	const std::string hex = "mask = 0xFF00";
	const std::vector<SyntaxSpan> hexSpans = highlightLine(hex, SyntaxFormat::Lua);
	SyntaxSpan const* num = firstOf(hexSpans, SyntaxToken::Number);
	REQUIRE(num != nullptr);
	CHECK(spanText(hex, *num) == "0xFF00");

	const std::string open = "s = \"unclosed";
	const std::vector<SyntaxSpan> openSpans = highlightLine(open, SyntaxFormat::Lua);
	requireGapFreeCover(openSpans, open.size());
	SyntaxSpan const* str = firstOf(openSpans, SyntaxToken::String);
	REQUIRE(str != nullptr);
	CHECK(str->end == open.size());	// runs to end of line
}

//--- IniLike ----------------------------------------------------------------

TEST_CASE("IniLike highlights a section header", "[syntax]")
{
	const std::string line = "[Font.9]";
	const std::vector<SyntaxSpan> spans = highlightLine(line, SyntaxFormat::IniLike);
	requireGapFreeCover(spans, line.size());
	SyntaxSpan const* section = firstOf(spans, SyntaxToken::Section);
	REQUIRE(section != nullptr);
	CHECK(spanText(line, *section) == "[Font.9]");
}

TEST_CASE("IniLike highlights the leading key and a hash comment", "[syntax]")
{
	const std::string line = "roughness 0.35 # matte";
	const std::vector<SyntaxSpan> spans = highlightLine(line, SyntaxFormat::IniLike);
	requireGapFreeCover(spans, line.size());

	SyntaxSpan const* key = firstOf(spans, SyntaxToken::Keyword);
	REQUIRE(key != nullptr);
	CHECK(spanText(line, *key) == "roughness");
	CHECK(key->begin == 0);

	SyntaxSpan const* num = firstOf(spans, SyntaxToken::Number);
	REQUIRE(num != nullptr);
	CHECK(spanText(line, *num) == "0.35");

	SyntaxSpan const* comment = firstOf(spans, SyntaxToken::Comment);
	REQUIRE(comment != nullptr);
	CHECK(spanText(line, *comment) == "# matte");
	CHECK(comment->end == line.size());
}

TEST_CASE("IniLike highlights a localisation reference and a string", "[syntax]")
{
	const std::string line = "text = @menu.play \"fallback\"";
	const std::vector<SyntaxSpan> spans = highlightLine(line, SyntaxFormat::IniLike);
	requireGapFreeCover(spans, line.size());
	SyntaxSpan const* ref = firstOf(spans, SyntaxToken::Reference);
	REQUIRE(ref != nullptr);
	CHECK(spanText(line, *ref) == "@menu.play");
	SyntaxSpan const* str = firstOf(spans, SyntaxToken::String);
	REQUIRE(str != nullptr);
	CHECK(spanText(line, *str) == "\"fallback\"");
}

//--- Markup -----------------------------------------------------------------

TEST_CASE("Markup highlights an XML tag and an attribute string", "[syntax]")
{
	const std::string line = "<Setting key=\"physics\" value=\"1\"/>";
	const std::vector<SyntaxSpan> spans = highlightLine(line, SyntaxFormat::Markup);
	requireGapFreeCover(spans, line.size());
	SyntaxSpan const* tag = firstOf(spans, SyntaxToken::Section);
	REQUIRE(tag != nullptr);
	CHECK(spanText(line, *tag) == "<Setting");
	SyntaxSpan const* str = firstOf(spans, SyntaxToken::String);
	REQUIRE(str != nullptr);
	CHECK(spanText(line, *str) == "\"physics\"");	// attribute value = String
}

TEST_CASE("Markup treats a JSON key as a section and a value as a string",
	"[syntax]")
{
	const std::string line = "  \"fps\": 24,";
	const std::vector<SyntaxSpan> spans = highlightLine(line, SyntaxFormat::Markup);
	requireGapFreeCover(spans, line.size());
	SyntaxSpan const* key = firstOf(spans, SyntaxToken::Section);
	REQUIRE(key != nullptr);
	CHECK(spanText(line, *key) == "\"fps\"");	// followed by ':'
	SyntaxSpan const* num = firstOf(spans, SyntaxToken::Number);
	REQUIRE(num != nullptr);
	CHECK(spanText(line, *num) == "24");

	const std::string value = "  \"name\": \"orkige\"";
	const std::vector<SyntaxSpan> vspans =
		highlightLine(value, SyntaxFormat::Markup);
	// exactly one Section (the key) and one String (the value)
	int sections = 0, strings = 0;
	for (SyntaxSpan const& s : vspans)
	{
		sections += (s.token == SyntaxToken::Section);
		strings += (s.token == SyntaxToken::String);
	}
	CHECK(sections == 1);
	CHECK(strings == 1);
}

TEST_CASE("Markup highlights a single-line XML comment", "[syntax]")
{
	const std::string line = "<a/> <!-- a note --> <b/>";
	const std::vector<SyntaxSpan> spans = highlightLine(line, SyntaxFormat::Markup);
	requireGapFreeCover(spans, line.size());
	SyntaxSpan const* comment = firstOf(spans, SyntaxToken::Comment);
	REQUIRE(comment != nullptr);
	CHECK(spanText(line, *comment) == "<!-- a note -->");
}

//--- pathological + boundaries ----------------------------------------------

TEST_CASE("a very long line stays a valid gap-free cover", "[syntax]")
{
	std::string line;
	line.reserve(60000);
	for (int i = 0; i < 10000; ++i)
	{
		line += "abc123 ";	// identifiers + numbers + spaces
	}
	for (SyntaxFormat fmt : { SyntaxFormat::Lua, SyntaxFormat::IniLike,
		SyntaxFormat::Markup })
	{
		const std::vector<SyntaxSpan> spans = highlightLine(line, fmt);
		requireGapFreeCover(spans, line.size());
	}
}

TEST_CASE("an unterminated section/comment runs to end of line", "[syntax]")
{
	const std::string ini = "[UnclosedSection";
	const std::vector<SyntaxSpan> iniSpans =
		highlightLine(ini, SyntaxFormat::IniLike);
	requireGapFreeCover(iniSpans, ini.size());
	SyntaxSpan const* section = firstOf(iniSpans, SyntaxToken::Section);
	REQUIRE(section != nullptr);
	CHECK(section->end == ini.size());

	const std::string xml = "<tag <!-- unclosed comment";
	const std::vector<SyntaxSpan> xmlSpans =
		highlightLine(xml, SyntaxFormat::Markup);
	requireGapFreeCover(xmlSpans, xml.size());
	SyntaxSpan const* comment = firstOf(xmlSpans, SyntaxToken::Comment);
	REQUIRE(comment != nullptr);
	CHECK(comment->end == xml.size());
}

//--- extension mapping ------------------------------------------------------

TEST_CASE("syntaxFormatForExtension maps the engine text kinds", "[syntax]")
{
	CHECK(syntaxFormatForExtension(".lua") == SyntaxFormat::Lua);
	CHECK(syntaxFormatForExtension("lua") == SyntaxFormat::Lua);	// dotless
	CHECK(syntaxFormatForExtension(".LUA") == SyntaxFormat::Lua);	// case
	CHECK(syntaxFormatForExtension(".oui") == SyntaxFormat::IniLike);
	CHECK(syntaxFormatForExtension(".omat") == SyntaxFormat::IniLike);
	CHECK(syntaxFormatForExtension(".oshape") == SyntaxFormat::IniLike);
	CHECK(syntaxFormatForExtension(".orkproj") == SyntaxFormat::Markup);
	CHECK(syntaxFormatForExtension(".xlf") == SyntaxFormat::Markup);
	CHECK(syntaxFormatForExtension(".json") == SyntaxFormat::Markup);
	CHECK(syntaxFormatForExtension(".olevels") == SyntaxFormat::Markup);
	CHECK(syntaxFormatForExtension(".md") == SyntaxFormat::PlainText);
	CHECK(syntaxFormatForExtension(".txt") == SyntaxFormat::PlainText);
	CHECK(syntaxFormatForExtension(".bin") == SyntaxFormat::PlainText);
}

TEST_CASE("syntaxTokenColor is opaque and differs across tokens", "[syntax]")
{
	for (bool dark : { true, false })
	{
		const unsigned int text = syntaxTokenColor(SyntaxToken::Text, dark);
		const unsigned int str = syntaxTokenColor(SyntaxToken::String, dark);
		CHECK((text & 0xFFu) == 0xFFu);		// full alpha
		CHECK((str & 0xFFu) == 0xFFu);
		CHECK(text != str);					// tokens are visually distinct
	}
}
