/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorIdeServer.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorIdeServer_h__28_7_2026__12_00_00__
#define __EditorIdeServer_h__28_7_2026__12_00_00__

#include "EditorIdeProtocol.h"

#include <core_debugnet/DebugSocket.h>
#include <core_debugnet/HttpServer.h>
#include <core_debugnet/Json.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// EditorIdeServer.h - the transport half of the editor's Claude-IDE integration
// (@see EditorIdeProtocol.h for the pure protocol, Docs/claude-ide.md for the
// reference). It makes the running editor auto-discoverable as `claude`'s IDE:
//
//   * it writes the discovery lock ~/.claude/ide/<port>.lock (our WebSocket
//     port, pid, workspace root, auth token) and removes it on shutdown, with
//     stale-lock hygiene (a lock whose owner pid is gone may be reclaimed);
//   * it hosts an MCP server over WebSocket on a loopback HttpServer - the SAME
//     upgrade seam the browser debug link rides (EditorBrowserServe) - speaking
//     initialize / tools/list / tools/call, the tools mapped onto live editor
//     state (open documents, the focused selection, parse diagnostics, openFile
//     into the embedded editor); and
//   * it pushes selection_changed notifications so the caret context flows to
//     claude live.
//
// OPT-IN, like the MCP control endpoint: main starts it only when the editor
// runs interactively with --claude-ide / ORKIGE_CLAUDE_IDE (the editor_ide
// selfcheck opts in explicitly), so no ordinary run or automated test opens a
// socket or writes a lock. Honest silence when off. openDiff is not backed yet
// (a proposed-change diff view); it refuses honestly and the caller applies the
// edit directly - the EditorLineDiff hunk machinery is the future v2 backing.
namespace Orkige
{
	class EditorIdeServer
	{
	public:
		//! the MCP protocol version advertised on initialize
		static const std::string MCP_PROTOCOL_VERSION;

		EditorIdeServer();
		~EditorIdeServer();

		//! @brief listen on port (0 = an ephemeral loopback port, read back with
		//! getPort()), mint the handshake auth token, and arm the WebSocket
		//! upgrade takeover. Binds 127.0.0.1 ONLY. false on a socket failure.
		bool start(unsigned short port);
		//! stop listening, drop every WebSocket client, remove the lock file
		void stop();
		bool isListening() const { return mServer.isListening(); }
		unsigned short getPort() const { return mServer.getPort(); }
		//! the handshake token the client must present (the lock's authToken)
		std::string const& getToken() const { return mToken; }

		//! @brief write ~/.claude/ide/<port>.lock naming this server. The lock's
		//! workspaceFolders are @p workspaceFolders (the open project root, or
		//! empty). Rewrites when the workspace changes; a pre-existing lock at
		//! our path is reclaimed only when its owner pid is gone. false when the
		//! home dir / lock dir is unavailable.
		bool writeLockFile(std::vector<std::string> const& workspaceFolders);
		//! remove our lock file (no-op when we never wrote one / it is not ours)
		void removeLockFile();
		//! the lock file path we wrote ("" when none) - the selfcheck reads it
		std::string const& getLockPath() const { return mLockPath; }

		//! accept/read/dispatch/notify - call once per frame, never blocks
		void update(OrkigeEditor::IdeContext const& context);

		//! the number of connected WebSocket clients (the selfcheck asserts it)
		std::size_t clientCount() const { return mConnections.size(); }

	private:
		//! turn one parsed HTTP request into its response (the WS upgrade, auth
		//! checked against the lock token; everything else refused)
		HttpResponse handleHttp(HttpRequest const& request);
		//! dispatch one JSON-RPC message from a client; returns the response
		//! object, or sets isNotification (no reply is sent for notifications)
		JsonValue dispatchJsonRpc(JsonValue const& request, bool& isNotification,
			OrkigeEditor::IdeContext const& context);
		//! run one tools/call and build the MCP tool result
		JsonValue runToolCall(JsonValue const& params,
			OrkigeEditor::IdeContext const& context);
		//! send selection_changed to every client when the caret moved
		void pushSelectionChange(OrkigeEditor::IdeContext const& context);
		//! @brief prime ONE just-initialized client with the current active-file
		//! selection (a selection_changed push). A client that connected AFTER the
		//! open file was focused never sees it via the change stream, so this is
		//! sent right after its MCP initialize handshake; a reconnect re-primes.
		//! No-op when no document is the active editor.
		void sendInitialState(WebSocketConnection& connection,
			OrkigeEditor::IdeContext const& context);

		HttpServer mServer;
		std::string mToken;			//!< the handshake auth token (lock's authToken)
		std::string mLockPath;		//!< the lock file we wrote ("" = none)
		//! the workspace the current lock names (rewrite when it changes)
		std::vector<std::string> mLockWorkspace;
		//! live WebSocket clients (adopted from the HttpServer takeover)
		std::vector<std::unique_ptr<WebSocketConnection>> mConnections;
		//! sockets the takeover handed over mid-update, adopted at the next pump
		std::vector<std::pair<DebugSocketUtil::SocketHandle, std::string>>
			mPendingSockets;
		//! the last selection we notified (diffed to avoid re-sending)
		OrkigeEditor::IdeSelection mLastSelection;
		bool mSelectionSeeded = false;	//!< have we captured a first selection

		EditorIdeServer(EditorIdeServer const&) = delete;
		EditorIdeServer& operator=(EditorIdeServer const&) = delete;
	};

	//! @brief the editor_ide selfcheck: a worker thread drives a FAKE IDE client
	//! (a raw socket doing the WebSocket handshake with the lock token, then an
	//! MCP conversation) against the editor's own live IDE server - initialize,
	//! getWorkspaceFolders, getOpenEditors, an openFile that must land in the
	//! Script panel, getDiagnostics round-trip, and a rejected handshake with a
	//! bad token. Proves the whole endpoint headlessly. Pumped on the main
	//! thread; the client work runs on the worker (a same-thread blocking client
	//! would deadlock the single-threaded server pump).
	class EditorIdeSelfTest
	{
	public:
		~EditorIdeSelfTest();
		//! @brief begin driving 127.0.0.1:port with the handshake token.
		//! @p openFileTarget is the path the openFile leg asks the editor to open
		//! while connected (the main loop asserts it appeared in the Script panel,
		//! and the client asserts its live selection_changed push arrives).
		//! @p preOpenTarget is a path the MAIN loop opened BEFORE the client
		//! connected; the client asserts the initialize handshake is followed by an
		//! initial selection_changed naming it AND that getCurrentSelection reports
		//! it as the active file with no user action ("" disables that leg).
		void begin(unsigned short port, std::string const& token,
			std::string const& openFileTarget,
			std::string const& preOpenTarget);
		void update();
		bool active() const { return mActive.load(); }
		bool done() const { return mDone.load(); }
		bool passed() const { return mPassed.load(); }
		//! the path the openFile leg asked the editor to open (the main loop
		//! cross-checks that the Script panel really opened it) - "" until run
		std::string openedFile();

	private:
		void run(unsigned short port);
		std::thread mThread;
		std::string mToken;
		std::string mOpenFileTarget;
		std::string mPreOpenTarget;	//!< opened before connect (initial-push leg)
		std::mutex mOpenedMutex;
		std::string mOpenedFile;		//!< guarded by mOpenedMutex
		std::atomic<bool> mActive{ false };
		std::atomic<bool> mDone{ false };
		std::atomic<bool> mPassed{ false };
	};
}

#endif //__EditorIdeServer_h__28_7_2026__12_00_00__
