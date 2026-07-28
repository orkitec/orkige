/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalScreenTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
//! The libvterm-backed VT screen model driven headlessly with scripted escape
//! sequences: plain text, SGR colour, cursor movement, erase, scrollback push,
//! and the DECSET modes (application cursor keys / bracketed paste) the input
//! encoder consumes.
#include <catch2/catch_test_macros.hpp>

#include <EditorTerminalScreen.h>

using namespace OrkigeEditor;

TEST_CASE("terminal screen: plain text lands in the grid",
	"[unit][editor][terminal]")
{
	EditorTerminalScreen screen(20, 4);
	screen.write("hello");
	CHECK(screen.cell(0, 0).glyph == "h");
	CHECK(screen.cell(0, 4).glyph == "o");
	CHECK(screen.cursor().row == 0);
	CHECK(screen.cursor().col == 5);
	CHECK(screen.dumpVisible().substr(0, 5) == "hello");
}

TEST_CASE("terminal screen: SGR colour resolves to RGB",
	"[unit][editor][terminal]")
{
	EditorTerminalScreen screen(20, 2);
	// bright red foreground, then a letter, then reset
	screen.write("\x1b[91mR\x1b[0mX");
	TermCell red = screen.cell(0, 0);
	CHECK(red.glyph == "R");
	CHECK(red.fg.r > 150);
	CHECK(red.fg.g < 120);
	CHECK(red.fg.b < 120);
	// after reset the next cell is the default foreground (not the same red)
	TermCell plain = screen.cell(0, 1);
	CHECK(plain.glyph == "X");
	CHECK_FALSE((plain.fg.r == red.fg.r && plain.fg.g == red.fg.g &&
		plain.fg.b == red.fg.b));
}

TEST_CASE("terminal screen: 24-bit truecolour", "[unit][editor][terminal]")
{
	EditorTerminalScreen screen(10, 2);
	// SGR 38;2;R;G;B sets an exact RGB foreground
	screen.write("\x1b[38;2;10;200;30mG");
	TermCell g = screen.cell(0, 0);
	CHECK(g.glyph == "G");
	CHECK(g.fg.r == 10);
	CHECK(g.fg.g == 200);
	CHECK(g.fg.b == 30);
}

TEST_CASE("terminal screen: cursor movement + erase",
	"[unit][editor][terminal]")
{
	EditorTerminalScreen screen(20, 4);
	// place the cursor at row 2, col 5 (CUP is 1-based) and write
	screen.write("\x1b[2;5Hmark");
	CHECK(screen.cursor().row == 1);
	CHECK(screen.cell(1, 4).glyph == "m");
	CHECK(screen.cell(1, 7).glyph == "k");
	// home + erase to end of screen clears it
	screen.write("\x1b[H\x1b[J");
	CHECK(screen.cell(1, 4).glyph != "m");
	CHECK(screen.dumpVisible().find("mark") == std::string::npos);
}

TEST_CASE("terminal screen: line wrap + scrollback push",
	"[unit][editor][terminal]")
{
	EditorTerminalScreen screen(4, 2, 100);
	// three CRLF-separated lines into a 2-row grid: the first scrolls off
	screen.write("AAA\r\nBBB\r\nCCC");
	CHECK(screen.scrollbackCount() >= 1);
	// the oldest scrollback line is the first one written
	CHECK(screen.scrollbackCell(0, 0).glyph == "A");
	// the visible grid now shows the later lines
	const std::string visible = screen.dumpVisible();
	CHECK(visible.find("CCC") != std::string::npos);
	CHECK(visible.find("AAA") == std::string::npos);
}

TEST_CASE("terminal screen: DECSET application cursor keys + bracketed paste",
	"[unit][editor][terminal]")
{
	EditorTerminalScreen screen(20, 4);
	CHECK_FALSE(screen.applicationCursorKeys());
	CHECK_FALSE(screen.bracketedPaste());

	// DECCKM on (mode 1), bracketed paste on (mode 2004)
	screen.write("\x1b[?1h");
	CHECK(screen.applicationCursorKeys());
	screen.write("\x1b[?2004h");
	CHECK(screen.bracketedPaste());

	// and back off
	screen.write("\x1b[?1l\x1b[?2004l");
	CHECK_FALSE(screen.applicationCursorKeys());
	CHECK_FALSE(screen.bracketedPaste());

	// a combined DECRST with multiple params also parses
	screen.write("\x1b[?1;2004h");
	CHECK(screen.applicationCursorKeys());
	CHECK(screen.bracketedPaste());
}

TEST_CASE("terminal screen: resize keeps the model consistent",
	"[unit][editor][terminal]")
{
	EditorTerminalScreen screen(10, 3);
	screen.write("row");
	screen.resize(40, 10);
	CHECK(screen.cols() == 40);
	CHECK(screen.rows() == 10);
	// the text survives the widen
	CHECK(screen.cell(0, 0).glyph == "r");
}
