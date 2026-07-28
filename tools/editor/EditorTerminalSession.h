/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalSession.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorTerminalSession_h__28_7_2026__12_00_00__
#define __EditorTerminalSession_h__28_7_2026__12_00_00__

//! @file EditorTerminalSession.h
//! @brief the pure, UI-free bookkeeping behind the Terminal panel's MULTIPLE
//! sessions and app-aware tab titles: cleaning a raw VT/OSC title down to a tab
//! label, classifying a detected program name into a glyph class (a recognised
//! agent CLI vs a plain shell), composing the tab label from the two available
//! signals (title wins, else the pty's foreground process name, else a numbered
//! fallback) and computing which tab becomes active after a close. Everything
//! here is a value-in/value-out function so it is unit-tested headlessly
//! (EditorTerminalSessionTests) with no ImGui, no pty and no VT library.

#include <cstdint>
#include <string>

namespace OrkigeEditor
{
	//! which glyph a tab shows for what is running inside it. Agent = a
	//! recognised terminal-agent CLI (a distinct icon); Terminal = anything
	//! else (the plain terminal glyph). The concrete Font Awesome codepoints
	//! live in terminalGlyphCodepoint() so the panel and the atlas glyph-range
	//! list stay in one truth.
	enum class TerminalGlyphClass
	{
		Terminal,
		Agent
	};

	//! @brief clean a raw window/OSC title into a short tab label. Whitespace is
	//! trimmed; when the title is a filesystem path or a path-prefixed command
	//! line (its leading token contains a '/' or starts with '~') it is reduced
	//! to the LEADING APP WORD - the basename of that first token - so a shell
	//! that reports its working directory ("/Users/me/dev/orkige") or full path
	//! ("/opt/homebrew/bin/fish -l") shows "orkige" / "fish". A plain title
	//! ("Claude Code") is returned trimmed but otherwise verbatim. Pure.
	std::string terminalCleanTitle(std::string const& raw);

	//! @brief classify a detected program name into a glyph class. A
	//! case-insensitive PREFIX match against the known agent-CLI names yields
	//! Agent; everything else is Terminal. The match list names programs the
	//! user runs, never a product referenced in UI text - the displayed name is
	//! always runtime data from the session. Pure.
	TerminalGlyphClass classifyTerminalApp(std::string const& name);

	//! @brief the Font Awesome 6 codepoint the panel draws for a glyph class
	//! (Agent = robot, Terminal = terminal). Kept beside the classifier so the
	//! EditorTheme icon glyph-range list has ONE table to mirror.
	std::uint32_t terminalGlyphCodepoint(TerminalGlyphClass glyphClass);

	//! a composed tab label: the display text plus the glyph class to draw.
	struct TerminalTabLabel
	{
		std::string			text;
		TerminalGlyphClass	glyph = TerminalGlyphClass::Terminal;
	};

	//! @brief compose a session's tab label from the two signals. TITLE WINS:
	//! a non-empty cleaned VT/OSC title is the label; otherwise the pty's
	//! foreground process name (cleaned the same way); otherwise the numbered
	//! fallback "Terminal <index1Based>". The glyph is Agent when EITHER the
	//! title or the process name classifies as an agent (a shell that sets no
	//! title still gets the robot from its foreground `claude`, and an agent
	//! that sets a descriptive title still gets it from that title). Pure.
	TerminalTabLabel terminalTabLabel(std::string const& vtTitle,
		std::string const& processName, int index1Based);

	//! @brief which list index is active after closing one tab. Given a list of
	//! @p count sessions, the index @p closedIndex being removed and the current
	//! @p activeIndex, returns the active index into the shrunk list of
	//! count-1 - the neighbour that slides into the closed slot (or the new last
	//! tab when the last was closed). Returns -1 when the list becomes empty.
	//! Pure.
	int terminalIndexAfterClose(int count, int closedIndex, int activeIndex);
}

#endif //__EditorTerminalSession_h__28_7_2026__12_00_00__
