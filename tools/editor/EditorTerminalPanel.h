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
//! @brief the dockable "Terminal" panel: a real pseudo-terminal (the OS pty
//! backend, EditorTerminalPty) rendered as a mono-font character grid (the VT
//! model EditorTerminalScreen) with keyboard input (the pure encoder
//! EditorTerminalKeys). It spawns the user's LOGIN shell so a bundled .app's
//! skinny PATH still resolves git/claude, and when the editor's MCP endpoint is
//! live it seeds the session's environment with the connection material and
//! shows the ready-made `claude mcp add ...` line, so an agent started inside is
//! one paste from controlling the very editor it lives in.
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

//! @brief draw the Terminal panel. `mcpUrl` / `mcpTokenFile` carry the live MCP
//! endpoint (empty when the editor was launched without --mcp-port): non-empty
//! seeds ORKIGE_MCP_URL / ORKIGE_MCP_TOKEN_FILE into the spawned shell AND shows
//! the connect-command hint; empty means no env and no hint (honest silence).
//! Never spawns a shell during automated runs.
void drawTerminalPanel(EditorState& state, ViewSettings& viewSettings,
	std::string const& mcpUrl, std::string const& mcpTokenFile, bool* visible);

namespace OrkigeEditor
{
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
}

#endif //__EditorTerminalPanel_h__28_7_2026__12_00_00__
