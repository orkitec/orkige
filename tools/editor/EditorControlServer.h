// EditorControlServer.h - the editor-side MCP endpoint.
//
// An MCP server hosted DIRECTLY IN THE EDITOR over Streamable HTTP (the MCP
// stable remote transport, spec 2025-03-26): a single `POST /mcp` speaking
// JSON-RPC 2.0 (initialize / tools/list / tools/call / notifications). Claude
// Code/Desktop connect to the editor as a REMOTE MCP server - no Python
// sidecar, no extra pip dependency (Util/orkige_mcp.py was retired).
//
// It REUSES the existing command handler wholesale - the same thin adapter over
// EditorCore + the EditorDocument free functions exposing ~17 editor verbs
// (open/save scene, open/close project, read the hierarchy, create/delete/
// reparent/rename objects, add/remove components, read/write the six typed
// property bundles, selection, undo/redo, screenshots, asset listing, console
// tail, play/stop translated onto the ONE player debug protocol). ONLY the wire
// framing changed: DebugMessage line-JSON over a raw DebugServer became HTTP/1.1
// + JSON-RPC over a hand-rolled loopback HttpServer (core_debugnet/HttpServer),
// with each verb surfaced as an MCP tool carrying a JSON inputSchema. The verb
// dispatch still works on an internal DebugMessage request/reply pair; the MCP
// layer converts a tool call's JSON arguments into that request and the reply
// back into MCP tool content.
//
// AUTH: the existing token-file scheme is kept - the editor writes a random
// secret to a file; the MCP client presents it as `Authorization: Bearer
// <token>`. MUTATION verbs are refused until a valid token authenticated the
// request (per-request, HTTP-idiomatic); read verbs are open (a loopback reader
// is harmless). Correlation is JSON-RPC's native `id`.
//
// OFF by default: main starts it only when --control-port / ORKIGE_CONTROL_PORT
// asks for it, so no existing run or test is affected.
//
// SSE: POST-only (single JSON response per request). The optional GET-SSE stream
// is not implemented (the tool surface is request/response; long ops - play boot
// - return an accepted result and are polled via get_state).
#ifndef ORKIGE_EDITORCONTROLSERVER_H_09072026
#define ORKIGE_EDITORCONTROLSERVER_H_09072026
#include "core_util/optr.h"
#include <core_debugnet/DebugProtocol.h>
#include <core_debugnet/HttpServer.h>
#include <core_debugnet/Json.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>


// forward declarations (the shells live in EditorApp.h; the header stays free
// of the SDL/ImGui pull EditorApp.h carries so main can include it cheaply)
struct EditorState;
struct PlaySession;
struct EditorConsole;
struct SceneRenderTarget;

namespace OrkigeEditor
{
	class GuiPreviewStage;	//!< the GUI Preview stage (GuiPreviewStage.h)
}

namespace Orkige
{
	class EditorCore;
	class GameObjectManager;
	//! @brief one asynchronous test run (run_tests -> get_test_results). Opaque
	//! here; defined in the .cpp. A worker thread does the build+ctest work and
	//! parks the structured verdict for a later get_test_results poll.
	struct EditorTestJob;
	//! @brief one asynchronous project export (export_project ->
	//! get_export_results). Opaque here; defined in the .cpp. A worker thread
	//! drives Util/orkige_export.py and parks the artifact path / error tail.
	struct EditorExportJob;

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
		//! the shared GUI Preview stage (the preview_ui verb drives it; the
		//! GUI Preview tab shares the same instance) - GuiPreviewStage.h
		OrkigeEditor::GuiPreviewStage* previewStage = nullptr;
	};

	//! @brief the editor's MCP endpoint: a loopback HttpServer plus the JSON-RPC
	//! dispatch that adapts MCP tool calls onto the verb handler. Pump
	//! update() once per frame.
	class EditorControlServer
	{
	public:
		//! internal verb reply types (the DebugMessage the handler builds)
		static const String MSG_OK;			//!< "ok" - a verb succeeded
		static const String MSG_ERR;		//!< "err" - a verb was refused/failed
		//! the MCP protocol version this server speaks (Streamable HTTP)
		static const String MCP_PROTOCOL_VERSION;

		EditorControlServer();
		~EditorControlServer();

		//! @brief listen on 127.0.0.1:port (0 = pick an ephemeral port, read it
		//! back with getPort()) and, when tokenFilePath is non-empty, write the
		//! freshly minted auth token there for the client to read. false on a
		//! socket bind or token-file write failure.
		bool start(unsigned short port, std::string const& tokenFilePath);
		//! stop listening, drop clients, delete the token file
		void stop();
		bool isListening() const { return mServer.isListening(); }
		unsigned short getPort() const { return mServer.getPort(); }
		//! the auth token the client must present (empty = auth off)
		std::string const& getToken() const { return mToken; }

		//! accept/read/dispatch/reply - call once per frame, never blocks
		void update(EditorControlContext const& context);

	private:
		//--- HTTP + JSON-RPC transport -----------------------
		//! turn one parsed HTTP request into its response (POST /mcp = JSON-RPC)
		HttpResponse handleHttp(HttpRequest const& request,
			EditorControlContext const& context);
		//! dispatch one JSON-RPC request object; returns the response object, or
		//! sets isNotification (no response is emitted for notifications)
		JsonValue dispatchJsonRpc(JsonValue const& request, bool authenticated,
			EditorControlContext const& context, bool& isNotification);
		//! run a tools/call: build the internal request, run the verb, convert
		//! the reply to MCP tool content (content[] + structuredContent/isError)
		JsonValue runToolCall(JsonValue const& params, bool authenticated,
			EditorControlContext const& context);
		//! the advertised tool list with JSON inputSchemas (tools/list result)
		static JsonValue buildToolList();

		//--- the verb handler (reused wholesale) ------
		//! dispatch one decoded verb request; the outcome lands in mReply/
		//! mReplyIsError (sendOk/sendErr buffer it instead of writing a socket)
		void runVerb(DebugMessage const& request,
			EditorControlContext const& context);
		void handleMessage(DebugMessage const& request,
			EditorControlContext const& context);
		//! buffer an "ok" reply carrying payload's fields/lists
		void sendOk(String const& req, DebugMessage& payload);
		//! buffer an "ok" reply with no payload
		void sendOk(String const& req);
		//! buffer an "err" reply (message)
		void sendErr(String const& req, String const& message);
		//! is the request allowed to run a mutation verb (auth gate)?
		bool requireAuth(String const& req);

		//! join every finished/outstanding test-run worker (called on stop)
		void joinTestJobs();
		//! join every finished/outstanding export worker (called on stop)
		void joinExportJobs();

		HttpServer mServer;
		std::string mToken;				//!< the auth secret (empty = auth off)
		std::string mTokenFilePath;		//!< where the token was written ("" = none)
		bool mAuthenticated = false;	//!< the current request presented a valid token
		DebugMessage mReply;			//!< the verb's buffered reply
		bool mReplyIsError = false;		//!< was the buffered reply an error
		//! outstanding/finished async test runs, keyed by their generated jobId;
		//! run_tests appends, get_test_results reads, stop() joins the workers
		std::vector<Orkige::uptr<EditorTestJob>> mTestJobs;
		//! outstanding/finished async exports, same lifecycle as mTestJobs;
		//! export_project appends, get_export_results reads, stop() joins
		std::vector<Orkige::uptr<EditorExportJob>> mExportJobs;
	};

	//! @brief the in-process MCP endpoint self-test (the editor_control ctest).
	//! Opens a RAW TCP socket to the editor's OWN control port on a background
	//! thread and drives a real MCP conversation over Streamable HTTP: a
	//! JSON-RPC `initialize` handshake, `notifications/initialized`, `tools/
	//! list`, then `tools/call`s - create_object (authed, + verify it appears in
	//! list_hierarchy), an AUTH-REJECTED create_object (wrong/absent token on a
	//! mutation) and a screenshot to a temp path (+ verify the file was written)
	//! - asserting MCP-compliant JSON-RPC responses (id echo, result shape) at
	//! every step. It also drives the test-runner tools: list_tests (a known
	//! unit test must appear), run_tests + get_test_results on ONE already-built
	//! unit test (build:false) asserting the structured pass tally, and a
	//! throwaway failing CTest tree so the failure list + logTail parse is
	//! exercised. This proves the whole C++ MCP endpoint headlessly, no Python.
	//! The socket work runs on a worker thread (the server is pumped on the main
	//! thread, so a same-thread blocking client would deadlock); main polls the
	//! done/passed verdict and turns it into the process exit code.
	class EditorControlSelfTest
	{
	public:
		~EditorControlSelfTest();
		//! @brief start driving the MCP endpoint at 127.0.0.1:port with the given
		//! auth token; the screenshot verb writes to screenshotPath. With
		//! runtimeDebug set, run the RUNTIME DEBUG conversation instead of the
		//! edit-world one: boot Play over MCP, then pause/step/inspect/mutate/
		//! screenshot the RUNNING game and stop (the editor_control_debug ctest,
		//! which needs the built player).
		void begin(unsigned short port, std::string const& token,
			std::string const& screenshotPath, bool runtimeDebug = false);
		//! poll the worker (the GameObjectManager param is unused - the endpoint
		//! is verified entirely through its JSON-RPC responses)
		void update(GameObjectManager& manager);
		bool active() const { return mActive.load(); }
		bool done() const { return mDone.load(); }
		bool passed() const { return mPassed.load(); }

	private:
		//! the whole conversation, run on the worker thread
		void run(unsigned short port);

		std::thread mThread;
		std::string mToken;
		std::string mScreenshotPath;
		bool mRuntimeDebug = false;		//!< run the runtime-debug conversation
		std::atomic<bool> mActive{ false };
		std::atomic<bool> mDone{ false };
		std::atomic<bool> mPassed{ false };
	};
}

#endif // ORKIGE_EDITORCONTROLSERVER_H_09072026
