/**************************************************************
	created:	2026/07/11 at 12:00
	filename: 	TextEntryEditTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless tests for the pure GuiTextEntry editing model
	(GuiTextEdit.h): insert / backspace / delete, caret motion, home/end
	and the code-point-aware max length + UTF-8 handling, plus the LINE model
	a multi-line field edits in (line bounds, the code-point column, up/down
	navigation, newline insert and the joins backspace/delete make across a
	line break). The rendered field (SDL text-input routing, caret blink,
	focus, soft wrap, the scrolled line window) is exercised by the
	demo_textentry and player_gallery selfchecks.
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

//=========================================================
//=== the multi-line model (logical lines over the same buffer)
//=========================================================

TEST_CASE("TextEntryEdit finds the bounds of the line a caret sits on",
	"[unit][ui]")
{
	//            0123 4567 89
	String text = "one\ntwo\nup";
	// inside the first line
	REQUIRE(lineStart(text, 0) == 0);
	REQUIRE(lineStart(text, 2) == 0);
	REQUIRE(lineEnd(text, 0) == 3);		// at the first '\n'
	// the caret sitting ON the newline still belongs to the line it ends
	REQUIRE(lineStart(text, 3) == 0);
	REQUIRE(lineEnd(text, 3) == 3);
	// right after the newline: the second line
	REQUIRE(lineStart(text, 4) == 4);
	REQUIRE(lineEnd(text, 4) == 7);
	// the last line runs to the end of the buffer
	REQUIRE(lineStart(text, 9) == 8);
	REQUIRE(lineEnd(text, 9) == text.size());
	// out-of-range clamps rather than reading past the end
	REQUIRE(lineEnd(text, 999) == text.size());
}

TEST_CASE("TextEntryEdit counts logical lines and code-point columns",
	"[unit][ui]")
{
	String text = "ab\ncd";
	REQUIRE(lineCount(text) == 2);
	REQUIRE(lineIndexOf(text, 0) == 0);
	REQUIRE(lineIndexOf(text, 2) == 0);
	REQUIRE(lineIndexOf(text, 3) == 1);
	REQUIRE(columnOf(text, 0) == 0);
	REQUIRE(columnOf(text, 2) == 2);
	REQUIRE(columnOf(text, 4) == 1);	// second line, one code point in

	// an empty buffer is still ONE line; a trailing newline opens an empty one
	REQUIRE(lineCount(String()) == 1);
	REQUIRE(lineCount(String("a\n")) == 2);
	REQUIRE(lineStart(String("a\n"), 2) == 2);

	// columns count CODE POINTS, never bytes (a two-byte glyph is one column)
	String utf8 = "\xC3\xA9x";			// an accented glyph then 'x'
	REQUIRE(columnOf(utf8, 3) == 2);
}

TEST_CASE("TextEntryEdit up/down keep the column and clamp on short lines",
	"[unit][ui]")
{
	//            0123456 78 9...
	String text = "long___\nab\nlong___";
	size_t caret = 5;					// column 5 on line 1
	REQUIRE_FALSE(moveUp(text, caret));	// already on the first line
	REQUIRE(caret == 5);

	REQUIRE(moveDown(text, caret));		// -> line 2 ("ab") clamps to its end
	REQUIRE(caret == 10);				// 8 + 2
	REQUIRE(columnOf(text, caret) == 2);

	REQUIRE(moveDown(text, caret));		// -> line 3, column 2 kept
	REQUIRE(columnOf(text, caret) == 2);
	REQUIRE(lineIndexOf(text, caret) == 2);

	REQUIRE_FALSE(moveDown(text, caret));	// last line: no move

	REQUIRE(moveUp(text, caret));		// back up, still column 2
	REQUIRE(lineIndexOf(text, caret) == 1);
	REQUIRE(columnOf(text, caret) == 2);
}

TEST_CASE("TextEntryEdit home/end work within a line", "[unit][ui]")
{
	String text = "one\ntwo";
	size_t caret = 5;					// inside "two"
	moveLineHome(text, caret);
	REQUIRE(caret == 4);
	moveLineEnd(text, caret);
	REQUIRE(caret == 7);
	// on the first line they stay inside it
	caret = 1;
	moveLineEnd(text, caret);
	REQUIRE(caret == 3);				// at the '\n', not the buffer end
	moveLineHome(text, caret);
	REQUIRE(caret == 0);
}

TEST_CASE("TextEntryEdit inserts a newline and joins lines back",
	"[unit][ui]")
{
	String text = "abcd";
	size_t caret = 2;
	REQUIRE(insertNewline(text, caret, 0));
	REQUIRE(text == "ab\ncd");
	REQUIRE(caret == 3);				// past the break
	REQUIRE(lineCount(text) == 2);

	// backspace at the start of a line JOINS it with the previous one - no
	// special case, the newline is an ordinary single-byte character
	REQUIRE(backspace(text, caret));
	REQUIRE(text == "abcd");
	REQUIRE(caret == 2);
	REQUIRE(lineCount(text) == 1);

	// forward delete at the end of a line joins the same way
	REQUIRE(insertNewline(text, caret, 0));
	caret = 2;							// before the '\n'
	REQUIRE(del(text, caret));
	REQUIRE(text == "abcd");
	REQUIRE(lineCount(text) == 1);

	// the newline obeys the code-point budget like any other insert
	String full = "abc";
	size_t end = full.size();
	REQUIRE_FALSE(insertNewline(full, end, 3));
	REQUIRE(full == "abc");
}
