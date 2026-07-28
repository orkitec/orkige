/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalKeys.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// EditorTerminalKeys.cpp - the pure key -> VT byte-sequence encoder. The tables
// follow xterm's well-known conventions: cursor/edit/function keys as CSI/SS3
// sequences with the standard modifier parameter, control chords as C0 bytes.
// No engine, no libvterm - drivable headlessly by EditorTerminalKeysTests.
#include "EditorTerminalKeys.h"

#include <cstdint>
#include <string>

namespace OrkigeEditor
{
	namespace
	{
		//! the xterm modifier parameter: 1 + shift + 2*alt + 4*ctrl. Returns 1
		//! when no modifier is held (the "no modifier" sentinel xterm uses).
		int modifierParam(TermMods mods)
		{
			int value = 1;
			if (mods.shift) { value += 1; }
			if (mods.alt) { value += 2; }
			if (mods.ctrl) { value += 4; }
			return value;
		}

		bool anyMod(TermMods mods)
		{
			return mods.ctrl || mods.shift || mods.alt;
		}

		//! a CSI cursor/position key (final letter A/B/C/D/H/F). Unmodified it is
		//! ESC [ <letter> (or ESC O <letter> in application-cursor mode); with a
		//! modifier it is always ESC [ 1 ; <param> <letter> (CSI form).
		std::string csiLetter(char letter, TermMods mods, bool applicationCursor)
		{
			if (anyMod(mods))
			{
				return std::string("\x1b[1;") +
					std::to_string(modifierParam(mods)) + letter;
			}
			if (applicationCursor)
			{
				return std::string("\x1bO") + letter;
			}
			return std::string("\x1b[") + letter;
		}

		//! a CSI tilde key (ESC [ <num> ~). With a modifier it becomes
		//! ESC [ <num> ; <param> ~.
		std::string csiTilde(int number, TermMods mods)
		{
			std::string out = std::string("\x1b[") + std::to_string(number);
			if (anyMod(mods))
			{
				out += ";" + std::to_string(modifierParam(mods));
			}
			out += "~";
			return out;
		}

		//! an SS3 function key (F1..F4 = ESC O P/Q/R/S). With a modifier xterm
		//! switches to the CSI form ESC [ 1 ; <param> <letter>.
		std::string ss3Function(char letter, TermMods mods)
		{
			if (anyMod(mods))
			{
				return std::string("\x1b[1;") +
					std::to_string(modifierParam(mods)) + letter;
			}
			return std::string("\x1bO") + letter;
		}
	}

	std::string encodeTermKey(TermKey key, TermMods mods, bool applicationCursor)
	{
		switch (key)
		{
			case TermKey::None:		return std::string();
			// Enter is CR; the shell's line discipline maps it to the newline.
			// Alt+Enter prefixes ESC (the meta convention).
			case TermKey::Enter:
				return mods.alt ? std::string("\x1b\r") : std::string("\r");
			// most terminals send DEL (0x7f) for Backspace; Alt+Backspace
			// prefixes ESC (word-erase in readline).
			case TermKey::Backspace:
				return mods.alt ? std::string("\x1b\x7f") : std::string("\x7f");
			// Shift+Tab is the back-tab CSI Z; plain Tab is HT.
			case TermKey::Tab:
				return mods.shift ? std::string("\x1b[Z") : std::string("\t");
			case TermKey::Escape:	return std::string("\x1b");

			case TermKey::Up:		return csiLetter('A', mods, applicationCursor);
			case TermKey::Down:		return csiLetter('B', mods, applicationCursor);
			case TermKey::Right:	return csiLetter('C', mods, applicationCursor);
			case TermKey::Left:		return csiLetter('D', mods, applicationCursor);
			case TermKey::Home:		return csiLetter('H', mods, applicationCursor);
			case TermKey::End:		return csiLetter('F', mods, applicationCursor);

			case TermKey::Insert:	return csiTilde(2, mods);
			case TermKey::Delete:	return csiTilde(3, mods);
			case TermKey::PageUp:	return csiTilde(5, mods);
			case TermKey::PageDown:	return csiTilde(6, mods);

			case TermKey::F1:		return ss3Function('P', mods);
			case TermKey::F2:		return ss3Function('Q', mods);
			case TermKey::F3:		return ss3Function('R', mods);
			case TermKey::F4:		return ss3Function('S', mods);
			case TermKey::F5:		return csiTilde(15, mods);
			case TermKey::F6:		return csiTilde(17, mods);
			case TermKey::F7:		return csiTilde(18, mods);
			case TermKey::F8:		return csiTilde(19, mods);
			case TermKey::F9:		return csiTilde(20, mods);
			case TermKey::F10:		return csiTilde(21, mods);
			case TermKey::F11:		return csiTilde(23, mods);
			case TermKey::F12:		return csiTilde(24, mods);
		}
		return std::string();
	}

	std::string encodeControlChar(std::uint32_t codepoint, TermMods mods)
	{
		if (!mods.ctrl)
		{
			return std::string();
		}
		// Ctrl+A..Ctrl+Z -> 0x01..0x1a (case-insensitive)
		if (codepoint >= 'a' && codepoint <= 'z')
		{
			return std::string(1, static_cast<char>(codepoint - 'a' + 1));
		}
		if (codepoint >= 'A' && codepoint <= 'Z')
		{
			return std::string(1, static_cast<char>(codepoint - 'A' + 1));
		}
		// the classic non-letter control chords
		switch (codepoint)
		{
			case ' ':  case '@': return std::string(1, '\x00');	// NUL
			case '[':  return std::string(1, '\x1b');			// ESC
			case '\\': return std::string(1, '\x1c');			// FS
			case ']':  return std::string(1, '\x1d');			// GS
			case '^':  return std::string(1, '\x1e');			// RS
			case '_':  case '?': return std::string(1, '\x1f');	// US / Ctrl+_
			default:   return std::string();
		}
	}

	std::string encodeUtf8(std::uint32_t codepoint)
	{
		std::string out;
		if (codepoint <= 0x7f)
		{
			out.push_back(static_cast<char>(codepoint));
		}
		else if (codepoint <= 0x7ff)
		{
			out.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		}
		else if (codepoint <= 0xffff)
		{
			out.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		}
		else if (codepoint <= 0x10ffff)
		{
			out.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
			out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
			out.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
		}
		return out;
	}
}
