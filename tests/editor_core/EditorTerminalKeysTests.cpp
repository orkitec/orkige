/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalKeysTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
//! The pure terminal key -> VT byte-sequence encoder. Verifies the xterm
//! conventions the shell/TUI on the other end of the pty expects: cursor/edit/
//! function-key CSI/SS3 forms, the standard modifier parameter, control chords.
#include <catch2/catch_test_macros.hpp>

#include <EditorTerminalKeys.h>

using namespace OrkigeEditor;

namespace
{
	TermMods none() { return TermMods{}; }
}

TEST_CASE("terminal key encoder: plain keys", "[unit][editor][terminal]")
{
	CHECK(encodeTermKey(TermKey::None, none()).empty());
	CHECK(encodeTermKey(TermKey::Enter, none()) == "\r");
	CHECK(encodeTermKey(TermKey::Tab, none()) == "\t");
	CHECK(encodeTermKey(TermKey::Backspace, none()) == "\x7f");
	CHECK(encodeTermKey(TermKey::Escape, none()) == "\x1b");
}

TEST_CASE("terminal key encoder: cursor keys + application mode",
	"[unit][editor][terminal]")
{
	// normal (CSI) form
	CHECK(encodeTermKey(TermKey::Up, none()) == "\x1b[A");
	CHECK(encodeTermKey(TermKey::Down, none()) == "\x1b[B");
	CHECK(encodeTermKey(TermKey::Right, none()) == "\x1b[C");
	CHECK(encodeTermKey(TermKey::Left, none()) == "\x1b[D");
	CHECK(encodeTermKey(TermKey::Home, none()) == "\x1b[H");
	CHECK(encodeTermKey(TermKey::End, none()) == "\x1b[F");

	// DECCKM application-cursor form (SS3) when nothing is modified
	CHECK(encodeTermKey(TermKey::Up, none(), /*applicationCursor=*/true) ==
		"\x1bOA");
	CHECK(encodeTermKey(TermKey::Home, none(), true) == "\x1bOH");

	// a modifier forces the CSI 1 ; <param> form even in application mode
	TermMods shift;
	shift.shift = true;
	CHECK(encodeTermKey(TermKey::Up, shift, true) == "\x1b[1;2A");
	TermMods ctrl;
	ctrl.ctrl = true;
	CHECK(encodeTermKey(TermKey::Right, ctrl) == "\x1b[1;5C");
	TermMods altShift;
	altShift.alt = true;
	altShift.shift = true;
	// param = 1 + shift(1) + alt(2) = 4
	CHECK(encodeTermKey(TermKey::Left, altShift) == "\x1b[1;4D");
}

TEST_CASE("terminal key encoder: edit + function keys",
	"[unit][editor][terminal]")
{
	CHECK(encodeTermKey(TermKey::Insert, none()) == "\x1b[2~");
	CHECK(encodeTermKey(TermKey::Delete, none()) == "\x1b[3~");
	CHECK(encodeTermKey(TermKey::PageUp, none()) == "\x1b[5~");
	CHECK(encodeTermKey(TermKey::PageDown, none()) == "\x1b[6~");

	// modified tilde form: ESC [ n ; param ~
	TermMods ctrl;
	ctrl.ctrl = true;
	CHECK(encodeTermKey(TermKey::Delete, ctrl) == "\x1b[3;5~");

	CHECK(encodeTermKey(TermKey::F1, none()) == "\x1bOP");
	CHECK(encodeTermKey(TermKey::F4, none()) == "\x1bOS");
	CHECK(encodeTermKey(TermKey::F5, none()) == "\x1b[15~");
	CHECK(encodeTermKey(TermKey::F12, none()) == "\x1b[24~");

	// Shift+Tab is the back-tab
	TermMods shift;
	shift.shift = true;
	CHECK(encodeTermKey(TermKey::Tab, shift) == "\x1b[Z");
}

TEST_CASE("terminal key encoder: alt prefixes meta",
	"[unit][editor][terminal]")
{
	TermMods alt;
	alt.alt = true;
	CHECK(encodeTermKey(TermKey::Enter, alt) == "\x1b\r");
	CHECK(encodeTermKey(TermKey::Backspace, alt) == "\x1b\x7f");
}

TEST_CASE("terminal control-char encoder", "[unit][editor][terminal]")
{
	TermMods ctrl;
	ctrl.ctrl = true;
	// Ctrl+C -> ETX (0x03), the SIGINT byte
	CHECK(encodeControlChar('c', ctrl) == std::string(1, '\x03'));
	CHECK(encodeControlChar('C', ctrl) == std::string(1, '\x03'));
	// Ctrl+D -> EOT (0x04)
	CHECK(encodeControlChar('d', ctrl) == std::string(1, '\x04'));
	// Ctrl+A -> SOH (0x01), Ctrl+Z -> SUB (0x1a)
	CHECK(encodeControlChar('a', ctrl) == std::string(1, '\x01'));
	CHECK(encodeControlChar('z', ctrl) == std::string(1, '\x1a'));
	// the classic non-letter chords
	CHECK(encodeControlChar('[', ctrl) == std::string(1, '\x1b'));	// ESC
	CHECK(encodeControlChar(' ', ctrl) == std::string(1, '\x00'));	// NUL
	CHECK(encodeControlChar('\\', ctrl) == std::string(1, '\x1c'));

	// without Ctrl, nothing is produced (the caller treats it as text)
	CHECK(encodeControlChar('c', none()).empty());
	// a codepoint with no control mapping yields nothing
	CHECK(encodeControlChar('1', ctrl).empty());
}

TEST_CASE("terminal UTF-8 helper", "[unit][editor][terminal]")
{
	CHECK(encodeUtf8('A') == "A");
	CHECK(encodeUtf8(0x00e9) == "\xc3\xa9");			// e-acute
	CHECK(encodeUtf8(0x20ac) == "\xe2\x82\xac");		// euro sign
	CHECK(encodeUtf8(0x1f600) == "\xf0\x9f\x98\x80");	// grinning face
}
