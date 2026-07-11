/**************************************************************
	created:	2026/07/11 at 12:00
	filename: 	TextEntryEditTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the pure GuiTextEntry editing model
	(GuiTextEdit.h): insert / backspace / delete, caret motion, home/end
	and the code-point-aware max length + UTF-8 handling. The rendered field
	(SDL text-input routing, caret blink, focus) is exercised by the
	demo_textentry selfcheck.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <engine_gui/GuiTextEdit.h>

using namespace Orkige;
using namespace Orkige::TextEntryEdit;

TEST_CASE("TextEntryEdit inserts at the caret and advances it", "[unit][ui]")
{
	String text;
	size_t caret = 0;
	REQUIRE(insert(text, caret, "he", 0));
	REQUIRE(insert(text, caret, "llo", 0));
	REQUIRE(text == "hello");
	REQUIRE(caret == 5);
	// insert in the middle
	caret = 2;
	REQUIRE(insert(text, caret, "XYZ", 0));
	REQUIRE(text == "heXYZllo");
	REQUIRE(caret == 5);
}

TEST_CASE("TextEntryEdit backspace and delete remove one code point",
	"[unit][ui]")
{
	String text = "abc";
	size_t caret = 3;
	REQUIRE(backspace(text, caret));	// removes 'c'
	REQUIRE(text == "ab");
	REQUIRE(caret == 2);
	caret = 0;
	REQUIRE_FALSE(backspace(text, caret));	// nothing before the caret
	REQUIRE(del(text, caret));			// removes 'a'
	REQUIRE(text == "b");
	REQUIRE(caret == 0);
	REQUIRE(del(text, caret));			// removes 'b'
	REQUIRE(text.empty());
	REQUIRE_FALSE(del(text, caret));	// nothing at the caret
}

TEST_CASE("TextEntryEdit caret motion clamps at both ends", "[unit][ui]")
{
	String text = "abc";
	size_t caret = 1;
	moveLeft(text, caret);
	REQUIRE(caret == 0);
	moveLeft(text, caret);
	REQUIRE(caret == 0);	// clamped
	moveRight(text, caret);
	moveRight(text, caret);
	moveRight(text, caret);
	REQUIRE(caret == 3);
	moveRight(text, caret);
	REQUIRE(caret == 3);	// clamped
}

TEST_CASE("TextEntryEdit honours a code-point max length", "[unit][ui]")
{
	String text;
	size_t caret = 0;
	// max 3: the fourth code point is refused, the batch is clipped
	REQUIRE(insert(text, caret, "ab", 3));
	REQUIRE(insert(text, caret, "cd", 3));	// only 'c' fits
	REQUIRE(text == "abc");
	REQUIRE(caret == 3);
	REQUIRE_FALSE(insert(text, caret, "z", 3));	// full
	REQUIRE(text == "abc");
	REQUIRE(codepointCount(text) == 3);
}

TEST_CASE("TextEntryEdit steps whole UTF-8 code points", "[unit][ui]")
{
	// "é" is two UTF-8 bytes (0xC3 0xA9); the caret must never split it
	String text;
	size_t caret = 0;
	REQUIRE(insert(text, caret, "a\xC3\xA9""b", 0));
	REQUIRE(codepointCount(text) == 3);	// a, é, b
	REQUIRE(caret == text.size());
	moveLeft(text, caret);				// past 'b'
	moveLeft(text, caret);				// past the whole 'é'
	REQUIRE(caret == 1);
	// backspace removes the whole 'é' (both bytes), not a half code point
	size_t end = text.size();
	backspace(text, end);				// remove trailing 'b'
	backspace(text, end);				// remove 'é' as one unit
	REQUIRE(text == "a");
	REQUIRE(codepointCount(text) == 1);
}
