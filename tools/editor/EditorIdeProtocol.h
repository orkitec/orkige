/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorIdeProtocol.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorIdeProtocol_h__28_7_2026__12_00_00__
#define __EditorIdeProtocol_h__28_7_2026__12_00_00__

#include <core_debugnet/Json.h>

#include <string>
#include <vector>

// EditorIdeProtocol.h - the PURE half of the editor's Claude-IDE integration
// (@see EditorIdeServer.h for the socket/transport half, Docs/claude-ide.md for
// the protocol reference). This is the external, undocumented IDE-integration
// protocol Anthropic's `claude` CLI speaks to an editor: a discovery lock file
// under ~/.claude/ide/<port>.lock plus an MCP server over WebSocket whose tools
// are getWorkspaceFolders / getOpenEditors / getCurrentSelection /
// getDiagnostics / openFile / openDiff / close_tab. Everything here is data ->
// JSON with NO sockets and NO editor state, so it is exhaustively unit-tested
// (EditorIdeProtocolTests) and the transport layer stays a thin pump. The
// protocol is external and may shift between `claude` releases; the real-binary
// connection ctest is the drift alarm.
namespace OrkigeEditor
{
	//--- lock file model (~/.claude/ide/<port>.lock) ---------
	//! @brief the discovery lock `claude` reads to find a running IDE: the
	//! filename is the WebSocket port, the JSON body carries the pid, the open
	//! workspace roots, a display name, the transport ("ws") and the auth token
	//! the WebSocket handshake must present.
	struct IdeLockInfo
	{
		long pid = 0;
		std::vector<std::string> workspaceFolders;
		std::string ideName = "Orkige";
		std::string transport = "ws";
		std::string authToken;
	};
	//! serialize a lock to the compact single-line JSON `claude` expects
	std::string serializeIdeLock(IdeLockInfo const& info);
	//! parse a lock file body; false (out untouched) on malformed input
	bool parseIdeLock(std::string const& text, IdeLockInfo& out);
	//! @brief may a stale lock be overwritten? A lock whose owning process is
	//! gone is fair game; a live one is left alone (another IDE owns it).
	bool ideLockMayOverwrite(bool ownerPidAlive);

	//--- editor-state views the IDE tools report ------------
	//! one open document as getOpenEditors reports it
	struct IdeEditor
	{
		std::string absolutePath;
		std::string languageId;		//!< "lua"/"xml"/"json"/... (a hint for claude)
		bool active = false;		//!< the focused tab
		bool dirty = false;			//!< unsaved edits
	};
	//! one diagnostic getDiagnostics surfaces for an open document
	struct IdeDiagnostic
	{
		std::string absolutePath;
		int line = 0;				//!< 1-based problem line (0 = whole document)
		std::string message;
		std::string severity = "Error";	//!< "Error"/"Warning"/...
	};
	//! the active document's text selection (getCurrentSelection /
	//! selection_changed). Line/character are 0-based, LSP-style.
	struct IdeSelection
	{
		bool active = false;		//!< a document is focused with a known caret
		std::string absolutePath;
		std::string text;			//!< the selected text ("" for an empty caret)
		int startLine = 0;
		int startChar = 0;
		int endLine = 0;
		int endChar = 0;
	};

	//! @brief the single-threaded bridge between the editor UI and the IDE
	//! server: the script panel PUBLISHES the open documents / selection /
	//! diagnostics each frame, and the server writes back UI REQUESTS (open a
	//! file, close a tab) the panel consumes on its next draw. Lives on
	//! EditorState; both sides run on the main loop so no locking is needed.
	struct IdeSharedState
	{
		//--- published by the editor UI, read by the server ---
		std::vector<IdeEditor> openEditors;
		std::vector<IdeDiagnostic> diagnostics;
		IdeSelection selection;
		//--- written by the server, consumed by the editor UI ---
		std::string openFileRequest;	//!< a path to open ("" = none)
		int openFileLine = 0;			//!< 1-based reveal line (0 = none)
		std::string closeTabRequest;	//!< a tab name/path to close ("" = none)
		//--- the live server port (0 = off): the terminal seeds it into
		//! CLAUDE_CODE_SSE_PORT so a `claude` launched there auto-connects ---
		int ssePort = 0;
	};

	//! @brief what EditorIdeServer::update needs each frame: the live workspace
	//! root(s) plus the shared bridge. Assembled in main from EditorState.
	struct IdeContext
	{
		std::vector<std::string> workspaceFolders;
		IdeSharedState* shared = nullptr;
	};

	//! the claude languageId hint for a file path's extension
	std::string ideLanguageForPath(std::string const& path);
	//! file:// URI from an absolute path (percent-encoding the reserved bytes)
	std::string idePathToFileUri(std::string const& absolutePath);
	//! absolute path from a file:// URI ("" when it is not a file URI)
	std::string ideFileUriToPath(std::string const& uri);

	//--- MCP / JSON-RPC envelope helpers ---------------------
	Orkige::JsonValue ideJsonRpcResult(Orkige::JsonValue const& id,
		Orkige::JsonValue const& result);
	Orkige::JsonValue ideJsonRpcError(Orkige::JsonValue const& id, int code,
		std::string const& message);
	//! a JSON-RPC notification object (no id): {jsonrpc, method, params}
	Orkige::JsonValue ideNotification(std::string const& method,
		Orkige::JsonValue const& params);

	//! the MCP `initialize` result (protocolVersion/capabilities/serverInfo)
	Orkige::JsonValue ideInitializeResult(std::string const& protocolVersion,
		std::string const& serverName, std::string const& serverVersion);
	//! the advertised MCP tool list (tools/list result's `tools` array)
	Orkige::JsonValue ideToolList();

	//--- tool result content ---------------------------------
	//! an MCP tool result carrying one text block: {content:[{type,text}]}
	Orkige::JsonValue ideToolText(std::string const& text);
	//! an MCP tool ERROR result (text block + isError:true)
	Orkige::JsonValue ideToolError(std::string const& message);

	//--- tool result bodies (the text block is itself a JSON string) --
	Orkige::JsonValue ideWorkspaceFoldersResult(
		std::vector<std::string> const& roots);
	Orkige::JsonValue ideOpenEditorsResult(
		std::vector<IdeEditor> const& editors);
	Orkige::JsonValue ideSelectionResult(IdeSelection const& selection);
	//! getDiagnostics: filterPath empty = every open document
	Orkige::JsonValue ideDiagnosticsResult(
		std::vector<IdeDiagnostic> const& diagnostics,
		std::string const& filterPath);

	//! the params for a selection_changed notification (also the inner body
	//! getCurrentSelection reports)
	Orkige::JsonValue ideSelectionParams(IdeSelection const& selection);
	//! have two selections diverged enough to re-notify claude?
	bool ideSelectionChanged(IdeSelection const& a, IdeSelection const& b);
}

#endif //__EditorIdeProtocol_h__28_7_2026__12_00_00__
