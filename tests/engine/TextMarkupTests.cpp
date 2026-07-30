/********************************************************************
	created:	Thursday 2026/07/30 at 10:00
	filename: 	TextMarkupTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Unit tests for the PURE inline rich-text parser (engine_gui/TextMarkup): the
// grammar (colour spans, font spans, inline sprites, the '[[' escape), the
// nesting rule, and every malformed-input verdict - an unknown tag, a bad hex
// colour, an unterminated tag, a stray close and a span left open all have to
// leave readable text plus one diagnostic, never a crash and never eaten text.
// No font, no atlas, no renderer - the measurement half is exercised in
// WrapLayoutTests against a real baked font.
#include <catch2/catch_test_macros.hpp>

#include <engine_gui/TextMarkup.h>

#include <string>

using namespace Orkige;

namespace
{
	//! the concatenated text of every TEXT run - what the reader actually sees
	std::string visibleText(TextMarkup::Parse const& parsed)
	{
		std::string all;
		for (TextMarkup::Run const& run : parsed.runs)
		{
			if (run.kind == TextMarkup::Run::RK_Text)
			{
				all += run.text;
			}
		}
		return all;
	}
}

TEST_CASE("TextMarkup: plain text is ONE run and reports no markup",
	"[unit][gui][markup]")
{
	TextMarkup::Parse parsed;
	TextMarkup::parse("Hello world", parsed);
	REQUIRE(parsed.runs.size() == 1);
	CHECK(parsed.runs[0].kind == TextMarkup::Run::RK_Text);
	CHECK(parsed.runs[0].text == String("Hello world"));
	CHECK_FALSE(parsed.runs[0].hasColour);
	CHECK(parsed.runs[0].fontRef.empty());
	// the flag a caller uses to assert that markup mode leaves plain text alone
	CHECK_FALSE(parsed.sawMarkup);
	CHECK(parsed.diagnostics.empty());

	// empty text is no runs at all (not one empty run)
	TextMarkup::Parse empty;
	TextMarkup::parse("", empty);
	CHECK(empty.runs.empty());
	CHECK(empty.diagnostics.empty());
}

TEST_CASE("TextMarkup: a colour span colours exactly its own run",
	"[unit][gui][markup]")
{
	TextMarkup::Parse parsed;
	TextMarkup::parse("+50 [c=FF8800]gold[/c] earned", parsed);
	REQUIRE(parsed.runs.size() == 3);
	CHECK(parsed.sawMarkup);
	CHECK(parsed.diagnostics.empty());

	CHECK(parsed.runs[0].text == String("+50 "));
	CHECK_FALSE(parsed.runs[0].hasColour);

	CHECK(parsed.runs[1].text == String("gold"));
	REQUIRE(parsed.runs[1].hasColour);
	CHECK(parsed.runs[1].colour[0] == 1.0f);					// FF
	CHECK(parsed.runs[1].colour[1] == 136.0f / 255.0f);			// 88
	CHECK(parsed.runs[1].colour[2] == 0.0f);					// 00
	CHECK(parsed.runs[1].colour[3] == 1.0f);					// implicit alpha

	CHECK(parsed.runs[2].text == String(" earned"));
	CHECK_FALSE(parsed.runs[2].hasColour);

	// the eight-digit form carries an explicit alpha
	TextMarkup::Parse withAlpha;
	TextMarkup::parse("[c=00FF0080]half[/c]", withAlpha);
	REQUIRE(withAlpha.runs.size() == 1);
	CHECK(withAlpha.runs[0].colour[3] == 128.0f / 255.0f);

	// the tag NAME is case-insensitive, the hex digits are either case
	TextMarkup::Parse mixedCase;
	TextMarkup::parse("[C=ff0000]red[/C]", mixedCase);
	REQUIRE(mixedCase.runs.size() == 1);
	REQUIRE(mixedCase.runs[0].hasColour);
	CHECK(mixedCase.runs[0].colour[0] == 1.0f);
}

TEST_CASE("TextMarkup: spans NEST per attribute and restore the outer one",
	"[unit][gui][markup]")
{
	TextMarkup::Parse parsed;
	TextMarkup::parse("[c=FF0000]a[c=00FF00]b[/c]c[/c]d", parsed);
	REQUIRE(parsed.runs.size() == 4);
	// a: red, b: green (the inner span), c: red again, d: no span
	CHECK(parsed.runs[0].text == String("a"));
	CHECK(parsed.runs[0].colour[0] == 1.0f);
	CHECK(parsed.runs[1].text == String("b"));
	CHECK(parsed.runs[1].colour[1] == 1.0f);
	CHECK(parsed.runs[2].text == String("c"));
	CHECK(parsed.runs[2].colour[0] == 1.0f);
	CHECK(parsed.runs[2].colour[1] == 0.0f);
	CHECK(parsed.runs[3].text == String("d"));
	CHECK_FALSE(parsed.runs[3].hasColour);
	CHECK(parsed.diagnostics.empty());

	// colour and font are INDEPENDENT attributes: closing one keeps the other
	TextMarkup::Parse crossed;
	TextMarkup::parse("[c=FF0000][f=heading]big red[/c]still big[/f]", crossed);
	REQUIRE(crossed.runs.size() == 2);
	CHECK(crossed.runs[0].hasColour);
	CHECK(crossed.runs[0].fontRef == String("heading"));
	CHECK_FALSE(crossed.runs[1].hasColour);
	CHECK(crossed.runs[1].fontRef == String("heading"));
}

TEST_CASE("TextMarkup: a font span carries its reference verbatim (name or index)",
	"[unit][gui][markup]")
{
	TextMarkup::Parse byName;
	TextMarkup::parse("plain [f=heading]HEAD[/f] plain", byName);
	REQUIRE(byName.runs.size() == 3);
	CHECK(byName.runs[0].fontRef.empty());
	CHECK(byName.runs[1].fontRef == String("heading"));
	CHECK(byName.runs[2].fontRef.empty());

	// an index literal is a legal reference too (the atlas resolves both)
	TextMarkup::Parse byIndex;
	TextMarkup::parse("[f=24]x[/f]", byIndex);
	REQUIRE(byIndex.runs.size() == 1);
	CHECK(byIndex.runs[0].fontRef == String("24"));

	// a font NAME keeps its case - it is an id, not a keyword
	TextMarkup::Parse cased;
	TextMarkup::parse("[F=Heading]x[/f]", cased);
	REQUIRE(cased.runs.size() == 1);
	CHECK(cased.runs[0].fontRef == String("Heading"));
}

TEST_CASE("TextMarkup: an inline sprite is its own run and takes the open colour",
	"[unit][gui][markup]")
{
	TextMarkup::Parse parsed;
	TextMarkup::parse("+50 [sprite=coin] now", parsed);
	REQUIRE(parsed.runs.size() == 3);
	CHECK(parsed.runs[0].kind == TextMarkup::Run::RK_Text);
	CHECK(parsed.runs[1].kind == TextMarkup::Run::RK_Sprite);
	CHECK(parsed.runs[1].sprite == String("coin"));
	CHECK_FALSE(parsed.runs[1].hasColour);
	CHECK(parsed.runs[2].text == String(" now"));

	// a sprite inside a colour span is TINTED by it, like text is
	TextMarkup::Parse tinted;
	TextMarkup::parse("[c=FF8800][sprite=coin][/c]", tinted);
	REQUIRE(tinted.runs.size() == 1);
	REQUIRE(tinted.runs[0].kind == TextMarkup::Run::RK_Sprite);
	REQUIRE(tinted.runs[0].hasColour);
	CHECK(tinted.runs[0].colour[0] == 1.0f);
	CHECK(tinted.runs[0].colour[1] == 136.0f / 255.0f);
}

TEST_CASE("TextMarkup: '[[' writes a literal bracket, so any text is authorable",
	"[unit][gui][markup]")
{
	TextMarkup::Parse parsed;
	TextMarkup::parse("press [[A]] to jump", parsed);
	// the escape resolves; the unescaped ']' needs nothing (only '[' opens a tag)
	CHECK(visibleText(parsed) == "press [A]] to jump");
	CHECK(parsed.diagnostics.empty());
	CHECK(parsed.sawMarkup);

	// an escaped bracket right before a real tag still parses both
	TextMarkup::Parse mixed;
	TextMarkup::parse("[[c=FF0000] vs [c=FF0000]red[/c]", mixed);
	REQUIRE(mixed.runs.size() == 2);
	CHECK(mixed.runs[0].text == String("[c=FF0000] vs "));
	CHECK_FALSE(mixed.runs[0].hasColour);
	CHECK(mixed.runs[1].text == String("red"));
	CHECK(mixed.runs[1].hasColour);
	CHECK(mixed.diagnostics.empty());
}

TEST_CASE("TextMarkup: an unknown tag is drawn VERBATIM with one diagnostic",
	"[unit][gui][markup]")
{
	TextMarkup::Parse parsed;
	TextMarkup::parse("a [b]bold?[/b] c", parsed);
	// the text survives exactly as typed - the author sees their own tag
	CHECK(visibleText(parsed) == "a [b]bold?[/b] c");
	CHECK(parsed.diagnostics.size() == 2);	// the open and the close
	CHECK(parsed.diagnostics[0].find("[b]") != String::npos);

	// a colour tag with no argument, and one with an argument that is not hex
	TextMarkup::Parse badColour;
	TextMarkup::parse("[c]x[/c]", badColour);
	CHECK(visibleText(badColour).find("[c]") != String::npos);
	CHECK_FALSE(badColour.diagnostics.empty());

	TextMarkup::Parse notHex;
	TextMarkup::parse("[c=tomato]x[/c]", notHex);
	CHECK(visibleText(notHex).find("[c=tomato]") != String::npos);
	CHECK(visibleText(notHex).find('x') != std::string::npos);
	REQUIRE_FALSE(notHex.diagnostics.empty());
	CHECK(notHex.diagnostics[0].find("hex") != String::npos);

	// a hex body of the wrong LENGTH is refused too (3-digit CSS shorthand is
	// deliberately not part of the grammar)
	TextMarkup::Parse shortHex;
	TextMarkup::parse("[c=F80]x[/c]", shortHex);
	CHECK(visibleText(shortHex).find("[c=F80]") != String::npos);
	CHECK_FALSE(shortHex.diagnostics.empty());
}

TEST_CASE("TextMarkup: an unterminated tag keeps the rest of the text readable",
	"[unit][gui][markup]")
{
	TextMarkup::Parse parsed;
	TextMarkup::parse("score [c=FF0000 and the rest", parsed);
	CHECK(visibleText(parsed) == "score [c=FF0000 and the rest");
	REQUIRE(parsed.diagnostics.size() == 1);
	CHECK(parsed.diagnostics[0].find("unterminated") != String::npos);
	// nothing was styled, because nothing parsed
	for (TextMarkup::Run const& run : parsed.runs)
	{
		CHECK_FALSE(run.hasColour);
	}
}

TEST_CASE("TextMarkup: a stray close is dropped, an unclosed span runs to the end",
	"[unit][gui][markup]")
{
	// a close with nothing open: dropped (not drawn), one diagnostic
	TextMarkup::Parse stray;
	TextMarkup::parse("plain[/c]tail", stray);
	CHECK(visibleText(stray) == "plaintail");
	REQUIRE(stray.diagnostics.size() == 1);
	CHECK(stray.diagnostics[0].find("never opened") != String::npos);

	TextMarkup::Parse strayFont;
	TextMarkup::parse("plain[/f]tail", strayFont);
	CHECK(visibleText(strayFont) == "plaintail");
	CHECK(strayFont.diagnostics.size() == 1);

	// an unclosed span styles to the end of the text and says so
	TextMarkup::Parse unclosed;
	TextMarkup::parse("[c=FF0000]all the way", unclosed);
	REQUIRE(unclosed.runs.size() == 1);
	CHECK(unclosed.runs[0].text == String("all the way"));
	CHECK(unclosed.runs[0].hasColour);
	REQUIRE(unclosed.diagnostics.size() == 1);
	CHECK(unclosed.diagnostics[0].find("never closed") != String::npos);

	// an empty sprite name names nothing: verbatim + diagnostic
	TextMarkup::Parse emptySprite;
	TextMarkup::parse("[sprite=]", emptySprite);
	CHECK(visibleText(emptySprite) == "[sprite=]");
	CHECK(emptySprite.diagnostics.size() == 1);
}

TEST_CASE("TextMarkup: newlines and multi-byte text pass through untouched",
	"[unit][gui][markup]")
{
	// '\n' stays IN the run text (the wrap core turns it into a forced break)
	TextMarkup::Parse parsed;
	TextMarkup::parse("line one\n[c=FF0000]line two[/c]", parsed);
	REQUIRE(parsed.runs.size() == 2);
	CHECK(parsed.runs[0].text == String("line one\n"));
	CHECK(parsed.runs[1].text == String("line two"));

	// UTF-8 is opaque to the parser: only '[' is special
	TextMarkup::Parse utf8;
	TextMarkup::parse("[c=FF0000]\xE6\x97\xA5\xE6\x9C\xAC[/c]", utf8);
	REQUIRE(utf8.runs.size() == 1);
	CHECK(utf8.runs[0].text == String("\xE6\x97\xA5\xE6\x9C\xAC"));
	CHECK(utf8.diagnostics.empty());
}
