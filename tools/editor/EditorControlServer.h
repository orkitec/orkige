// EditorControlServer.h - the editor-side MCP control port (WP #80).
//
// A SECOND core_debugnet DebugServer instance, but living in the EDITOR
// process (not the player): it exposes the editor's operations - open/save
// scene, open/close project, read the hierarchy, create/delete/reparent/rename
// objects, add/remove components, read/write the six typed property bundles,
// selection, undo/redo, screenshots, asset listing, console tail - AND
// translates play-control verbs (play/stop/pause/resume/step) into the ONE
// existing player debug protocol (execution-plan ruling: never a second player
// port). An MCP host (Util/orkige_mcp.py) bridges Claude's tool calls to it.
//
// It reuses the debug transport wholesale (line-JSON over loopback TCP,
// DebugServer/DebugMessage - tested in tests/core/DebugProtocolTests.cpp) and
// adds two additive protocol conventions on top:
//   - request/response CORRELATION: a request may carry a "req" id; the reply
//     ("ok"/"err") echoes it so an async host matches answers to requests.
//   - an AUTH TOKEN: the editor writes a random secret to a file; the host
//     reads it and presents it in a "hello". MUTATION verbs are refused until
//     a valid hello authenticated the connection. Read verbs are open (a
//     loopback reader inspecting the editor is harmless; a mutation is not).
//
// OFF by default: main starts it only when --control-port / ORKIGE_CONTROL_PORT
// asks for it, so no existing run or test is affected.
//
// The handler is a THIN adapter: ~all verbs map onto existing EditorCore
// methods and the EditorDocument free functions (see the command->function
// table in the WP #80 API spec). It holds only pointers into main's owned
// objects (EditorControlContext) - no ownership, no UI (no ImGui/SDL here).
#ifndef ORKIGE_EDITORCONTROLSERVER_H_09072026
#define ORKIGE_EDITORCONTROLSERVER_H_09072026

#include <core_debugnet/DebugClient.h>
#include <core_debugnet/DebugServer.h>

#include <chrono>
#include <string>

// forward declarations (the shells live in EditorApp.h; the header stays free
// of the SDL/ImGui pull EditorApp.h carries so main can include it cheaply)
struct EditorState;
struct PlaySession;
struct EditorConsole;
struct SceneRenderTarget;

namespace Orkige
{
	class EditorCore;
	class GameObjectManager;

	//! @brief everything the control-port handler bridges to (all owned by
	//! main; the server holds raw pointers, never ownership). The Project the
	//! play-control and asset verbs need is reached through state->project.
	struct EditorControlContext
	{
		EditorState* state = nullptr;
		EditorCore* core = nullptr;
		PlaySession* play = nullptr;
		EditorConsole* console = nullptr;
		SceneRenderTarget* sceneTarget = nullptr;
		GameObjectManager* gameObjectManager = nullptr;
	};

	//! @brief the editor's MCP control server: a loopback DebugServer plus the
	//! command handler that adapts its verbs onto the editor. Single-client
	//! like the player link. Pump update() once per frame.
	class EditorControlServer
	{
	public:
		//! reply message types (echo the request's "req"): success / failure
		static const String MSG_OK;			//!< "ok" - a verb succeeded
		static const String MSG_ERR;		//!< "err" - a verb was refused/failed

		EditorControlServer();
		~EditorControlServer();

		//! @brief listen on 127.0.0.1:port (0 = pick an ephemeral port, read it
		//! back with getPort()) and, when tokenFilePath is non-empty, write the
		//! freshly minted auth token there for the host to read. false on a
		//! socket bind or token-file write failure.
		bool start(unsigned short port, std::string const& tokenFilePath);
		//! stop listening, drop the client, delete the token file
		void stop();
		bool isListening() const { return mServer.isListening(); }
		unsigned short getPort() const { return mServer.getPort(); }
		//! the auth token the host must present in its hello
		std::string const& getToken() const { return mToken; }

		//! accept/read/dispatch/reply - call once per frame, never blocks
		void update(EditorControlContext const& context);

	private:
		//! dispatch one decoded request and send its ok/err reply
		void handleMessage(DebugMessage const& request,
			EditorControlContext const& context);
		//! send an "ok" reply carrying payload's fields/lists, "req" echoed
		void sendOk(String const& req, DebugMessage& payload);
		//! send an "ok" reply with no payload beyond the correlation id
		void sendOk(String const& req);
		//! send an "err" reply (message + "req" echoed)
		void sendErr(String const& req, String const& message);
		//! is the connection allowed to run a mutation verb right now?
		bool requireAuth(String const& req);

		DebugServer mServer;
		std::string mToken;				//!< the auth secret (empty = auth off)
		std::string mTokenFilePath;		//!< where the token was written ("" = none)
		bool mAuthenticated = false;	//!< a valid hello arrived on this client
	};

	//! @brief the in-process control-port self-test (the editor_control ctest).
	//! Connects a DebugClient to the editor's OWN control server and drives a
	//! representative verb sequence - hello (auth), list_hierarchy, create_
	//! object (+ verify it exists locally), get/set the Transform bundle
	//! (mutation + request/response correlation) and a screenshot to a temp
	//! path (+ verify the file was written) - asserting every reply. This
	//! proves the whole C++ bridge headlessly, no Python needed. Pumped once
	//! per frame from main, like the existing playtest hooks; sets a pass/fail
	//! verdict main turns into the process exit code.
	class EditorControlSelfTest
	{
	public:
		//! start driving the control server at 127.0.0.1:port with the given
		//! auth token; the screenshot verb writes to screenshotPath
		void begin(unsigned short port, std::string const& token,
			std::string const& screenshotPath);
		//! pump the client + advance the state machine (manager is used for the
		//! local objectExists assertion after create_object)
		void update(GameObjectManager& manager);
		bool active() const { return mActive; }
		bool done() const { return mDone; }
		bool passed() const { return mPassed; }

	private:
		enum class Phase
		{
			Connecting, Hello, Hierarchy, Create, GetTransform,
			SetTransform, VerifyTransform, Screenshot, Done, Failed
		};
		//! send a request of the given type, remember its correlation id
		void send(String const& type, DebugMessage& message);
		//! fail the run with a logged reason
		void fail(String const& reason);

		DebugClient mClient;
		Phase mPhase = Phase::Connecting;
		String mOutstandingReq;			//!< correlation id awaiting a reply
		bool mRequestSent = false;		//!< current phase already sent its request
		unsigned int mReqCounter = 0;	//!< monotonic correlation id source
		std::string mToken;
		std::string mScreenshotPath;
		std::string mCreatedId;			//!< id create_object minted
		bool mActive = false;
		bool mDone = false;
		bool mPassed = false;
		std::chrono::steady_clock::time_point mDeadline;
	};
}

#endif // ORKIGE_EDITORCONTROLSERVER_H_09072026
