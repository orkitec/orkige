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

#include <string>

// EditorState / ViewSettings are the editor's global shared-state structs
// (EditorApp.h); the panel takes them by reference like every sibling panel.
struct EditorState;
struct ViewSettings;

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

	//! @brief the headless terminal selfcheck (ORKIGE_EDITOR_TERMINAL_TEST):
	//! spawns a REAL pty running a scripted echo, asserts the VT grid seam,
	//! types through the input seam and asserts the child received the bytes,
	//! then closes and asserts the child died. Returns a process exit code:
	//! 0 pass, 2 fail, 77 skip (no pty/shell available). Window-independent -
	//! it runs and exits before the render boot.
	int runTerminalSelfCheck();
}

#endif //__EditorTerminalPanel_h__28_7_2026__12_00_00__
