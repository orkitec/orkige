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
#include <vector>

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

	//! a recognised terminal-agent CLI, in the fixed order of the classifier's
	//! name table. Each maps to one PRIVATE-USE codepoint whose glyph is a
	//! runtime-GENERATED badge (a signature-tinted mark, baked into the default
	//! UI-font atlas), so a dock-tab title identifies its tenant at a glance.
	//! `None` = not a recognised agent (the plain terminal glyph). `Generic` is
	//! the reserved fallback badge for a future recognised-but-unlisted agent.
	enum class TerminalAgent
	{
		None = -1,
		Claude = 0,
		Codex,
		Opencode,
		Aider,
		Gemini,
		Generic,
		Count
	};

	//! @brief classify a detected program name into a specific agent (None when
	//! it is not a recognised agent CLI). Case-insensitive prefix match, the same
	//! table classifyTerminalApp() uses. Pure.
	TerminalAgent terminalAgentOf(std::string const& name);

	//! @brief classify a raw VT/OSC TITLE into a specific agent by looking for a
	//! known agent name as the title's leading word OR anywhere in it as a whole
	//! word (case-insensitive, with a boundary check so "raider" never matches
	//! "aider"). None when no agent word is present. This is the broader,
	//! title-side classifier: an agent that announces itself in its window title
	//! ("Claude Code", "claude - /path") classifies even before the foreground
	//! process name is first polled, and on a platform with no foreground-process
	//! signal. Pure.
	TerminalAgent terminalAgentInTitle(std::string const& title);

	//! @brief the CANONICAL display name of a recognised agent ("Claude",
	//! "Codex", ...) - the STABLE tab-label text a classified session shows,
	//! never the live status-ticker title the agent streams into the VT title.
	//! Empty for None. Pure.
	std::string terminalAgentDisplayName(TerminalAgent agent);

	//! @brief the STICKY per-session agent classification transition. Given the
	//! session's @p current classification and this frame's two signals (the
	//! polled foreground @p processName and the live @p vtTitle), returns the new
	//! sticky classification:
	//!   - a foreground process that IS a known agent (re)classifies to it - the
	//!     authoritative signal;
	//!   - a foreground process that is a plain shell / other program (a non-empty
	//!     name that is a KNOWN SHELL - @see terminalIsShellName) means the agent
	//!     EXITED - declassify to None. An unknown non-shell name (node, python -
	//!     the agent CLIs are interpreter programs whose launchers exec to their
	//!     runtime moments after classifying) HOLDS the classification;
	//!   - with NO usable process signal (empty name - before the first poll, or a
	//!     platform without one) a session already classified STAYS put (a
	//!     status-ticker title must never declassify it), and an unclassified one
	//!     may classify from the title alone.
	//! So a shell -> `claude` -> foreground becomes node -> status-ticker titles
	//! -> `claude` exits sequence classifies to Claude and holds there until the
	//! foreground reverts to the shell. Pure.
	TerminalAgent terminalUpdateStickyAgent(TerminalAgent current,
		std::string const& processName, std::string const& vtTitle);

	//! @brief is the cleaned process name a known interactive/login SHELL (the
	//! only foreground return that means "the agent exited")? Pure.
	bool terminalIsShellName(std::string const& name);

	//! @brief whether a Unicode codepoint is one the default UI text font is
	//! expected to carry: false for C0/C1 control codes and the symbol / emoji /
	//! dingbat ranges a text UI font lacks (which would draw a '?' tofu box), true
	//! for ordinary text. A conservative, font-independent heuristic (the tooltip
	//! and the tab-label leading strip run purely, with no ImGui atlas). Pure.
	bool terminalIsRenderableSymbol(std::uint32_t cp);

	//! @brief filter a raw UTF-8 title down to what the UI font can render:
	//! codepoints failing terminalIsRenderableSymbol() are DROPPED (never
	//! replaced with '?'), and the result is trimmed. So a status-ticker title
	//! like "✳ Check open file" (whose leading sparkle the UI font lacks)
	//! becomes "Check open file". Used for the tab TOOLTIP and to strip
	//! un-renderable leading symbols from an unclassified session's tab label so
	//! no tab ever leads with a tofu box. Pure.
	std::string terminalFilterRenderable(std::string const& utf8);

	//! @brief the PRIVATE-USE codepoint (U+E000 + ordinal) whose atlas glyph is
	//! the agent's generated badge. 0 for None. Pure.
	std::uint32_t terminalAgentBadgeCodepoint(TerminalAgent agent);

	//! a badge's signature tint (0-255 RGB) - the taste-flagged brand-family
	//! colour the generated mark carries.
	struct TerminalBadgeTint
	{
		unsigned char r = 0;
		unsigned char g = 0;
		unsigned char b = 0;
	};

	//! @brief the agent badge's signature tint. Pure.
	TerminalBadgeTint terminalAgentTint(TerminalAgent agent);

	//! @brief the structural stroke count of a generated mark - the radiating
	//! spokes of the Claude starburst (8) / the interlocking loops of the Codex
	//! ring (6) / 0 for the letter-monogram badges. Declared beside the generator
	//! so a unit test asserts the mark's construction parameter, not its pixels.
	//! Pure.
	int terminalAgentBadgeStrokeCount(TerminalAgent agent);

	//! @brief generate an agent's badge as a @p size x @p size RGBA8 image
	//! (row-major, 4 bytes/pixel, straight alpha; transparent where unpainted).
	//! Pure + deterministic: the same (agent,size) always yields identical bytes.
	//! Claude renders its coral radiating-asterisk mark, Codex the monochrome
	//! interlocking-ring mark, and every other agent a signature-tinted rounded
	//! square carrying the program's initial. Empty when @p size <= 0.
	//!
	//! These marks render EXCLUSIVELY to identify the third-party program running
	//! in a terminal session (nominative identification, the dock-icon precedent,
	//! owner-directed 2026-07-28); they are never a product logo or used anywhere
	//! else in the editor or docs. A later runtime vendor-icon discovery could
	//! replace these pixels (loading an installed app bundle's icon into the same
	//! atlas rect) with NO call-site change - this generator is the one seam.
	std::vector<unsigned char> terminalAgentBadgePixels(TerminalAgent agent,
		int size);

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

	//! a composed tab label: the display text, the glyph class to draw, and -
	//! when the tenant is a recognised agent - WHICH agent, so the panel draws
	//! that agent's generated badge glyph rather than the plain robot.
	struct TerminalTabLabel
	{
		std::string			text;
		TerminalGlyphClass	glyph = TerminalGlyphClass::Terminal;
		TerminalAgent		agent = TerminalAgent::None;
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

	//! @brief compose a session's tab label from its STICKY classification (see
	//! terminalUpdateStickyAgent) plus the two live signals. When @p stickyAgent
	//! is a recognised agent the label is STABLE: the agent's PUA badge glyph +
	//! its CANONICAL display name ("Claude", ...) - never the live status-ticker
	//! title the agent streams into its VT title. Otherwise it is the unclassified
	//! composition (cleaned title, else cleaned process name, else "Terminal N"),
	//! with un-renderable symbols stripped from the chosen text so no tab ever
	//! leads with a '?' tofu box. This is the composition the panel uses; the
	//! live VT title becomes the tab TOOLTIP (via terminalFilterRenderable). Pure.
	TerminalTabLabel terminalSessionTabLabel(TerminalAgent stickyAgent,
		std::string const& vtTitle, std::string const& processName,
		int index1Based);

	//! @brief which list index is active after closing one tab. Given a list of
	//! @p count sessions, the index @p closedIndex being removed and the current
	//! @p activeIndex, returns the active index into the shrunk list of
	//! count-1 - the neighbour that slides into the closed slot (or the new last
	//! tab when the last was closed). Returns -1 when the list becomes empty.
	//! Pure.
	int terminalIndexAfterClose(int count, int closedIndex, int activeIndex);

	//! an absolute grid coordinate: a line index (scrollback below the visible
	//! grid) and a column. `col` may equal `cols` (a selection END is exclusive,
	//! so the point just past the last column is a valid stop).
	struct TerminalGridPoint
	{
		int line = 0;
		int col = 0;
	};

	//! @brief map a mouse point to the absolute grid cell under it. @p px / @p py
	//! are the mouse in screen pixels, @p originX / @p originY the screen position
	//! of absolute line 0, column 0 (already scroll-adjusted). The result is
	//! CLAMPED so a drag past the grid edges still yields a valid stop: `line` to
	//! [0, totalLines-1], `col` to [0, cols] (cols inclusive, an exclusive end).
	//! Pure - the drag-selection hit test the panel and its unit test share, so a
	//! drag that leaves the visible rows still extends the selection deterministically.
	TerminalGridPoint terminalCellAtPoint(float px, float py, float originX,
		float originY, float cellW, float cellH, int cols, int totalLines);
}

#endif //__EditorTerminalSession_h__28_7_2026__12_00_00__
