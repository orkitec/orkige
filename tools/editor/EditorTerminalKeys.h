/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalKeys.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorTerminalKeys_h__28_7_2026__12_00_00__
#define __EditorTerminalKeys_h__28_7_2026__12_00_00__

//! @file EditorTerminalKeys.h
//! @brief a PURE key -> VT byte-sequence encoder for the embedded terminal.
//! Given a non-text key (arrows / navigation / function keys / Enter / Tab /
//! Backspace / Escape) plus modifier flags, it returns the exact xterm control
//! sequence a shell or full-screen TUI expects, and a companion helper turns a
//! Ctrl+<char> chord into its C0 control byte (Ctrl+C -> \x03). Printable text
//! never routes through here - it rides the platform IME/text-input path as
//! UTF-8. This unit is engine- and libvterm-free so the encoder table is
//! unit-tested headlessly (EditorTerminalKeysTests) and the VT parsing core
//! stays swappable behind EditorTerminalScreen.

#include <cstdint>
#include <string>

namespace OrkigeEditor
{
	//! the non-text keys the terminal encodes into escape sequences. Printable
	//! characters are NOT here - they arrive as UTF-8 text from the IME path.
	enum class TermKey
	{
		None,
		Enter, Backspace, Tab, Escape,
		Up, Down, Left, Right,
		Home, End, PageUp, PageDown, Insert, Delete,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12
	};

	//! keyboard modifier state at the moment the key was pressed. `super` (the
	//! Cmd key on macOS) is NEVER forwarded to the pty - it stays the editor's
	//! copy/paste modifier - so it is not part of the VT encoding, but callers
	//! pass the full set so one struct describes the chord.
	struct TermMods
	{
		bool ctrl = false;
		bool shift = false;
		bool alt = false;	//!< Left/Right Alt (Meta); prefixes ESC per xterm
	};

	//! @brief encode a non-text key press into its VT byte sequence.
	//! @param key the key (TermKey::None yields an empty string).
	//! @param mods the modifier state; the xterm CSI modifier parameter is
	//!        derived as 1 + shift + 2*alt + 4*ctrl and injected into the
	//!        sequence when any of the three is held.
	//! @param applicationCursor DECCKM state reported by the screen: when true,
	//!        the UNMODIFIED cursor/Home/End keys use the SS3 form (ESC O A)
	//!        that full-screen apps select, else the CSI form (ESC [ A).
	//! @return the bytes to write to the pty (UTF-8/ASCII), or "" for None.
	std::string encodeTermKey(TermKey key, TermMods mods,
		bool applicationCursor = false);

	//! @brief encode a Ctrl+<char> chord into its C0 control byte.
	//! Maps Ctrl+A..Ctrl+Z -> 0x01..0x1a, plus the classic Ctrl+@/[/\\/]/^/_
	//! and Ctrl+Space -> NUL. Returns "" when `mods.ctrl` is false or the
	//! codepoint has no control-code mapping (the caller then treats it as
	//! ordinary text). Case-insensitive on letters; `super`/`alt` are ignored
	//! here (alt-prefixing is handled by the text path).
	std::string encodeControlChar(std::uint32_t codepoint, TermMods mods);

	//! @brief encode a single Unicode codepoint as UTF-8. A small helper used by
	//! the synthetic-input path (tests, paste) where text is not already bytes.
	std::string encodeUtf8(std::uint32_t codepoint);
}

#endif //__EditorTerminalKeys_h__28_7_2026__12_00_00__
