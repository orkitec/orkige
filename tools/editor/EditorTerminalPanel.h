/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalPanel.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorTerminalPanel_h__28_7_2026__12_00_00__
#define __EditorTerminalPanel_h__28_7_2026__12_00_00__

//! @file EditorTerminalPanel.h
//! @brief the editor's embedded terminals: a real pseudo-terminal (the OS pty
//! backend, EditorTerminalPty) rendered as a mono-font character grid (the VT
//! model EditorTerminalScreen) with keyboard input (the pure encoder
//! EditorTerminalKeys). Each session is its OWN dockable ImGui window, sibling
//! tabs in the bottom dock group (the one-window-per-open-file pattern the code
//! editor uses), so a session's title rides the default UI font that carries the
//! terminal/robot icon glyphs. It spawns the user's LOGIN shell so a bundled
//! .app's skinny PATH still resolves git/claude, and when the editor's MCP
//! endpoint is live it seeds the session's environment with the connection
//! material and offers the ready-made `claude mcp add ...` line, so an agent
//! started inside is one paste from controlling the very editor it lives in.
//!
//! DELIBERATELY NOT exposed over MCP: a headless agent spawning shells in the
//! editor UI is out of scope (and a laundering path). The terminal is a human
//! affordance; agents drive the editor through the MCP verbs directly.

#include <cstddef>
#include <string>

// EditorState / ViewSettings are the editor's global shared-state structs
// (EditorApp.h); the panel takes them by reference like every sibling panel.
struct EditorState;
struct ViewSettings;

namespace OrkigeEditor
{
	class EditorTerminalScreen;
}

//! @brief draw the editor's terminal windows (one dockable window per session).
//! Called EVERY frame (zero sessions draws nothing) so every backgrounded
//! session keeps draining its pty. `mcpUrl` / `mcpTokenFile` carry the live MCP
//! endpoint (empty when the editor was launched without --mcp-port): non-empty
//! seeds ORKIGE_MCP_URL / ORKIGE_MCP_TOKEN_FILE into the spawned shell AND offers
//! the connect-command hint; empty means no env and no hint (honest silence).
//! `panelOpen` is the panel-registry "Terminal" flag: it MIRRORS "at least one
//! terminal window is open" - turning it on (View ▸ Terminal) with no sessions
//! spawns one, and the last window closing clears it. Never spawns a shell during
//! automated runs.
void drawTerminalPanel(EditorState& state, ViewSettings& viewSettings,
	std::string const& mcpUrl, std::string const& mcpTokenFile, bool* panelOpen);

namespace OrkigeEditor
{
	//! @brief request a new terminal session (View ▸ New Terminal / the in-window
	//! "+"). The session is spawned by the next drawTerminalPanel, which owns the
	//! project cwd + MCP env; the caller also turns the panel flag on so the
	//! window is drawn. Safe to call from a menu callback between frames.
	void terminalPanelRequestNewSession();

	//! @brief the number of live terminal sessions (test/inspection hook).
	int terminalPanelSessionCount();

	//! @brief terminate any live terminal child and release the session. Called
	//! at editor shutdown so a running shell never outlives the editor.
	void terminalPanelShutdown();

	//! @brief build the text of a linear selection over a terminal screen, in
	//! reading order with a newline between lines and trailing blanks trimmed.
	//! Line/col are ABSOLUTE (scrollback lines below the visible grid, so line
	//! [0, scrollbackCount()) index scrollback and higher index the visible
	//! grid). The two endpoints may be given in any order. This is the pure
	//! core the panel's Copy path and the selfcheck both use - no clipboard, no
	//! ImGui - so the copy grid-text can be asserted headlessly.
	std::string terminalSelectionText(EditorTerminalScreen& screen, int cols,
		int anchorLine, int anchorCol, int headLine, int headCol);

	//! @brief encode clipboard text for a paste into the pty. When @p bracketed
	//! (the app enabled DEC 2004 bracketed paste) the text is wrapped in
	//! ESC [ 200~ ... ESC [ 201~ so the app can tell a paste from typed input;
	//! otherwise the text is passed through verbatim. Plain Cmd/Ctrl+V works in
	//! either case - the framing is added only when the app asked for it.
	std::string terminalPasteEncoding(std::string const& clip, bool bracketed);

	//! @brief the headless terminal selfcheck (ORKIGE_EDITOR_TERMINAL_TEST):
	//! spawns a REAL pty running a scripted echo, asserts the VT grid seam,
	//! types through the input seam and asserts the child received the bytes,
	//! then closes and asserts the child died. Returns a process exit code:
	//! 0 pass, 2 fail, 77 skip (no pty/shell available). Window-independent -
	//! it runs and exits before the render boot.
	int runTerminalSelfCheck();

	// ---- real-event test seams (ORKIGE_EDITOR_TERMINAL_UITEST) --------------
	// The headless selfcheck above drives the pure seams; it CANNOT see a bug
	// where a real mouse drag + Cmd/Ctrl copy chord travels the actual ImGui
	// grid + focus path. These seams let the editor's real frame loop drive that
	// path under an automated run: opt the panel back in, publish the front
	// session's live grid geometry + selection + focus, and seed/locate
	// deterministic grid content the driver can target and assert.

	//! @brief run the terminal panel's REAL ImGui path even under an automated
	//! run (normally suppressed for pollution hygiene). Off by default; only the
	//! real-event copy selfcheck opts in.
	void terminalPanelSetAutomatedUiTest(bool enabled);

	//! @brief the front terminal session's live grid geometry + selection + focus,
	//! republished every frame the front session draws. `gridOriginX/Y` is the
	//! screen position of ABSOLUTE cell (line 0, col 0) - the same reference the
	//! mouse->cell hit test maps against - so a driver computes a cell's screen
	//! point as origin + (col*cellW, line*cellH). `frontWindowId` is the exact
	//! Begin() id string (badge + name + stable ###id) to SetWindowFocus.
	struct TerminalPanelProbe
	{
		bool	hasFrontSession = false;	//!< a session drew its grid this frame
		bool	spawned = false;			//!< the front session's pty is live
		bool	exited = false;				//!< the front session's shell died
		bool	windowFocused = false;		//!< the front session window has focus
		float	gridOriginX = 0.0f;
		float	gridOriginY = 0.0f;
		float	cellW = 1.0f;
		float	cellH = 1.0f;
		int		cols = 0;
		int		visibleRows = 0;
		int		scrollbackCount = 0;
		bool	hasSelection = false;
		std::string	selectionText;
		std::string	frontWindowId;			//!< the front window's Begin() id
		// follow-tail scroll state (real ImGui scroll, published post-decision)
		bool	followTail = false;			//!< the view is pinned to the newest line
		float	scrollY = 0.0f;				//!< the child's live vertical scroll
		float	scrollMaxY = 0.0f;			//!< the child's live max vertical scroll
		int		totalLines = 0;				//!< scrollback + visible rows this frame
		// Cmd/Ctrl+click link resolution under the cursor (the frame the link
		// modifier is held over the grid), for the path-open selfcheck leg
		bool	linkResolved = false;
		std::string	linkPath;				//!< the resolved absolute file to open
		int		linkLine = 0;				//!< 1-based :line (0 = none)
		// control-chord state, for the interrupt selfcheck leg: the PHYSICAL
		// modifiers exactly as the input path reads them (macOS un-swap applied)
		// and how many C0 control codes the panel has written to a child. Names
		// the failing tier - chord never seen vs byte sent but nothing happened.
		bool	physicalCtrl = false;		//!< the real Ctrl key is held
		bool	physicalCmd = false;		//!< the real Cmd/Super key is held
		int		controlCharsSent = 0;		//!< C0 control codes written so far
	};
	TerminalPanelProbe const& terminalPanelProbe();

	//! @brief write bytes straight into the first spawned session's VT screen -
	//! deterministic grid content without racing shell echo. No-op when no
	//! spawned session exists.
	void terminalPanelTestWrite(std::string const& bytes);

	//! @brief the ABSOLUTE line + start column of the first occurrence of
	//! `needle` in the first spawned session's grid (scrollback below the visible
	//! rows), or {-1,-1} when absent. Lets a driver target a known printed marker.
	struct TerminalProbeHit { int line = -1; int col = -1; };
	TerminalProbeHit terminalPanelTestFind(std::string const& needle);

	//! @brief how many grid lines of the first spawned session contain `needle`
	//! (same scan as terminalPanelTestFind). Two occurrences of a typed word is
	//! the proof a line-echoing child consumed it: the tty echo plus the child's
	//! own output.
	int terminalPanelTestCount(std::string const& needle);
}

#endif //__EditorTerminalPanel_h__28_7_2026__12_00_00__
