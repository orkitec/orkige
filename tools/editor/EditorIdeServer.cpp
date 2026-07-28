/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorIdeServer.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include "EditorIdeServer.h"

#include <core_debugnet/WebSocket.h>
#include <core_debug/DebugMacros.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#	include <process.h>
#	include <windows.h>
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	include <arpa/inet.h>
#	include <csignal>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <sys/types.h>
#	include <unistd.h>
#endif

namespace Orkige
{
	using OrkigeEditor::IdeContext;
	using OrkigeEditor::IdeLockInfo;
	using OrkigeEditor::IdeSelection;

	const std::string EditorIdeServer::MCP_PROTOCOL_VERSION = "2025-03-26";

	namespace
	{
		namespace fs = std::filesystem;

		//! is a process id still live (best-effort, for stale-lock hygiene)
		bool processAlive(long pid)
		{
			if (pid <= 0)
			{
				return false;
			}
#ifdef _WIN32
			HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
				FALSE, static_cast<DWORD>(pid));
			if (handle == nullptr)
			{
				return false;
			}
			DWORD exitCode = 0;
			const bool running = ::GetExitCodeProcess(handle, &exitCode) &&
				exitCode == STILL_ACTIVE;
			::CloseHandle(handle);
			return running;
#else
			// signal 0 probes existence without delivering anything; EPERM means
			// the process exists but is owned by another user (still alive)
			if (::kill(static_cast<pid_t>(pid), 0) == 0)
			{
				return true;
			}
			return errno == EPERM;
#endif
		}

		//! this process's id
		long currentPid()
		{
#ifdef _WIN32
			return static_cast<long>(::_getpid());
#else
			return static_cast<long>(::getpid());
#endif
		}

		//! the user's home directory (~) or "" when unavailable
		std::string homeDirectory()
		{
#ifdef _WIN32
			if (const char* profile = std::getenv("USERPROFILE"))
			{
				return profile;
			}
#endif
			if (const char* home = std::getenv("HOME"))
			{
				return home;
			}
			return std::string();
		}

		//! a random hex-ish token (the lock's authToken; 128 bits of CSPRNG)
		std::string mintToken()
		{
			std::random_device device;
			std::uniform_int_distribution<int> nibble(0, 15);
			static const char* HEX = "0123456789abcdef";
			std::string token;
			token.reserve(32);
			for (int i = 0; i < 32; ++i)
			{
				token += HEX[nibble(device)];
			}
			return token;
		}
	}
	//---------------------------------------------------------
	EditorIdeServer::EditorIdeServer() = default;
	//---------------------------------------------------------
	EditorIdeServer::~EditorIdeServer()
	{
		this->stop();
	}
	//---------------------------------------------------------
	bool EditorIdeServer::start(unsigned short port)
	{
		if (!mServer.start(port))	// binds 127.0.0.1 only (its contract)
		{
			return false;
		}
		mToken = mintToken();
		// the WebSocket upgrade takeover: an accepted (auth-checked) handshake
		// hands the raw socket here; adopted into a WebSocketConnection next pump
		mServer.setTakeoverHandler(
			[this](DebugSocketUtil::SocketHandle handle,
				std::string const& leftover)
		{
			mPendingSockets.emplace_back(handle, leftover);
		});
		return true;
	}
	//---------------------------------------------------------
	void EditorIdeServer::stop()
	{
		this->removeLockFile();
		mConnections.clear();
		for (auto& pending : mPendingSockets)
		{
			DebugSocketUtil::closeSocket(pending.first);
		}
		mPendingSockets.clear();
		mServer.stop();
		mToken.clear();
	}
	//---------------------------------------------------------
	bool EditorIdeServer::writeLockFile(
		std::vector<std::string> const& workspaceFolders)
	{
		const std::string home = homeDirectory();
		if (home.empty())
		{
			return false;
		}
		std::error_code ec;
		const fs::path ideDir = fs::path(home) / ".claude" / "ide";
		fs::create_directories(ideDir, ec);
		if (ec)
		{
			return false;
		}
		const fs::path lockPath =
			ideDir / (std::to_string(mServer.getPort()) + ".lock");
		// stale-lock hygiene: a pre-existing lock at our path is reclaimed only
		// when its owning process is gone (ours by ephemeral port, in practice)
		if (fs::exists(lockPath, ec))
		{
			std::ifstream existing(lockPath, std::ios::binary);
			std::ostringstream buffer;
			buffer << existing.rdbuf();
			IdeLockInfo prior;
			if (OrkigeEditor::parseIdeLock(buffer.str(), prior) &&
				prior.pid != currentPid() &&
				!OrkigeEditor::ideLockMayOverwrite(processAlive(prior.pid)))
			{
				return false;	// another live IDE owns this lock
			}
		}
		IdeLockInfo info;
		info.pid = currentPid();
		info.workspaceFolders = workspaceFolders;
		info.ideName = "Orkige";
		info.transport = "ws";
		info.authToken = mToken;
		std::ofstream out(lockPath, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			return false;
		}
		out << OrkigeEditor::serializeIdeLock(info);
		out.close();
#ifndef _WIN32
		// the token is a secret: keep the lock readable by its owner only
		fs::permissions(lockPath, fs::perms::owner_read | fs::perms::owner_write,
			fs::perm_options::replace, ec);
#endif
		mLockPath = lockPath.string();
		mLockWorkspace = workspaceFolders;
		return true;
	}
	//---------------------------------------------------------
	void EditorIdeServer::removeLockFile()
	{
		if (mLockPath.empty())
		{
			return;
		}
		std::error_code ec;
		fs::remove(mLockPath, ec);
		mLockPath.clear();
		mLockWorkspace.clear();
	}
	//---------------------------------------------------------
	HttpResponse EditorIdeServer::handleHttp(HttpRequest const& request)
	{
		HttpResponse response;
		if (WebSocketUtil::isUpgradeRequest(request))
		{
			// the handshake auth: the lock's authToken as a custom header. A
			// missing/wrong token is refused so a local process that never read
			// the lock cannot open the IDE surface.
			const String presented =
				request.header("x-claude-code-ide-authorization");
			if (mToken.empty() || presented == String(mToken.c_str()))
			{
				return WebSocketUtil::buildHandshakeResponse(request);
			}
			response.status = 401;
			response.reason = "Unauthorized";
			response.contentType = "text/plain";
			response.body = "invalid IDE authorization token\n";
			return response;
		}
		response.status = 404;
		response.reason = "Not Found";
		response.contentType = "text/plain";
		response.body = "orkige IDE endpoint: WebSocket upgrade only\n";
		return response;
	}
	//---------------------------------------------------------
	void EditorIdeServer::update(IdeContext const& context)
	{
		if (!mServer.isListening())
		{
			return;
		}
		// keep the lock's workspace current (auto-rewrite when the project moves)
		if (context.workspaceFolders != mLockWorkspace ||
			(mLockPath.empty() && !context.workspaceFolders.empty()))
		{
			this->writeLockFile(context.workspaceFolders);
		}

		mServer.update([this](HttpRequest const& request) -> HttpResponse
		{
			return this->handleHttp(request);
		});

		// adopt any sockets the takeover handed over during the pump
		for (auto& pending : mPendingSockets)
		{
			auto connection = std::make_unique<WebSocketConnection>();
			connection->attach(pending.first, pending.second);
			mConnections.push_back(std::move(connection));
			oDebugMsg("editor.ide", 0, "an IDE client connected (now " <<
				mConnections.size() << ")");
		}
		mPendingSockets.clear();

		// pump each client: drain its complete JSON-RPC messages, reply
		for (auto& connection : mConnections)
		{
			connection->pump();
			String raw;
			while (connection->isOpen() && connection->nextMessage(raw))
			{
				JsonValue request;
				if (!JsonValue::parse(raw, request))
				{
					connection->sendMessage(OrkigeEditor::ideJsonRpcError(
						JsonValue(), -32700, "parse error").serialize());
					continue;
				}
				bool isNotification = false;
				JsonValue reply =
					this->dispatchJsonRpc(request, isNotification, context);
				if (!isNotification)
				{
					connection->sendMessage(reply.serialize());
				}
				// once a client completes the MCP initialize handshake, push the
				// current active-file selection so a session that connected AFTER
				// the owner opened a file has that context with no user action (the
				// change stream below only carries LATER moves); reconnects, which
				// initialize afresh on a new connection, re-prime the same way.
				if (request.get("method").isString() &&
					request.get("method").asString() == "initialize")
				{
					this->sendInitialState(*connection, context);
				}
			}
		}
		// drop closed clients
		mConnections.erase(
			std::remove_if(mConnections.begin(), mConnections.end(),
				[](std::unique_ptr<WebSocketConnection> const& c)
				{ return !c->isOpen(); }),
			mConnections.end());

		this->pushSelectionChange(context);
	}
	//---------------------------------------------------------
	JsonValue EditorIdeServer::dispatchJsonRpc(JsonValue const& request,
		bool& isNotification, IdeContext const& context)
	{
		isNotification = false;
		if (!request.isObject() || !request.get("method").isString())
		{
			return OrkigeEditor::ideJsonRpcError(JsonValue(), -32600,
				"invalid request");
		}
		const bool hasId = request.has("id");
		isNotification = !hasId;
		const JsonValue id = hasId ? request.get("id") : JsonValue();
		const String method = request.get("method").asString();
		const JsonValue params = request.get("params");

		// MCP lifecycle notifications are acknowledged silently
		if (method.rfind("notifications/", 0) == 0)
		{
			isNotification = true;
			return JsonValue();
		}
		if (isNotification)
		{
			return JsonValue();
		}
		if (method == "initialize")
		{
			String protocolVersion(MCP_PROTOCOL_VERSION.c_str());
			if (params.isObject() &&
				params.get("protocolVersion").isString() &&
				!params.get("protocolVersion").asString().empty())
			{
				protocolVersion = params.get("protocolVersion").asString();
			}
			return OrkigeEditor::ideJsonRpcResult(id,
				OrkigeEditor::ideInitializeResult(
					std::string(protocolVersion.c_str()), "orkige-editor",
					ORKIGE_EDITOR_VERSION));
		}
		if (method == "ping")
		{
			return OrkigeEditor::ideJsonRpcResult(id, JsonValue::object());
		}
		if (method == "tools/list")
		{
			JsonValue result = JsonValue::object();
			result.set("tools", OrkigeEditor::ideToolList());
			return OrkigeEditor::ideJsonRpcResult(id, result);
		}
		if (method == "prompts/list")
		{
			JsonValue result = JsonValue::object();
			result.set("prompts", JsonValue::array());
			return OrkigeEditor::ideJsonRpcResult(id, result);
		}
		if (method == "tools/call")
		{
			return OrkigeEditor::ideJsonRpcResult(id,
				this->runToolCall(params, context));
		}
		return OrkigeEditor::ideJsonRpcError(id, -32601,
			"method not found: " + method);
	}
	//---------------------------------------------------------
	JsonValue EditorIdeServer::runToolCall(JsonValue const& params,
		IdeContext const& context)
	{
		if (!params.isObject() || !params.get("name").isString())
		{
			return OrkigeEditor::ideToolError("tools/call needs a 'name'");
		}
		const String name = params.get("name").asString();
		const JsonValue arguments = params.get("arguments");
		OrkigeEditor::IdeSharedState* shared = context.shared;

		if (name == "getWorkspaceFolders")
		{
			return OrkigeEditor::ideWorkspaceFoldersResult(
				context.workspaceFolders);
		}
		if (name == "getOpenEditors")
		{
			return OrkigeEditor::ideOpenEditorsResult(
				shared ? shared->openEditors
					: std::vector<OrkigeEditor::IdeEditor>());
		}
		if (name == "getCurrentSelection" || name == "getLatestSelection")
		{
			return OrkigeEditor::ideSelectionResult(
				shared ? shared->selection : IdeSelection());
		}
		if (name == "getDiagnostics")
		{
			std::string filterPath;
			if (arguments.get("uri").isString())
			{
				filterPath = OrkigeEditor::ideFileUriToPath(
					std::string(arguments.get("uri").asString().c_str()));
			}
			return OrkigeEditor::ideDiagnosticsResult(
				shared ? shared->diagnostics
					: std::vector<OrkigeEditor::IdeDiagnostic>(),
				filterPath);
		}
		if (name == "openFile")
		{
			if (!arguments.get("filePath").isString() ||
				arguments.get("filePath").asString().empty())
			{
				return OrkigeEditor::ideToolError(
					"openFile: missing required parameter 'filePath'");
			}
			const std::string filePath(
				arguments.get("filePath").asString().c_str());
			if (shared)
			{
				shared->openFileRequest = filePath;
				shared->openFileLine = 0;
			}
			return OrkigeEditor::ideToolText("Opened file: " + filePath);
		}
		if (name == "close_tab")
		{
			if (shared && arguments.get("tab_name").isString())
			{
				shared->closeTabRequest =
					std::string(arguments.get("tab_name").asString().c_str());
			}
			return OrkigeEditor::ideToolText("TAB_CLOSED");
		}
		if (name == "openDiff")
		{
			// v1: the proposed-change diff view is not backed yet (the
			// EditorLineDiff hunk machinery is the future v2 backing). An honest
			// tool error lets claude apply the edit directly instead of stalling.
			return OrkigeEditor::ideToolError(
				"openDiff is not supported by the Orkige editor; apply the "
				"edit directly");
		}
		return OrkigeEditor::ideToolError("unknown tool '" +
			std::string(name.c_str()) + "'");
	}
	//---------------------------------------------------------
	void EditorIdeServer::sendInitialState(WebSocketConnection& connection,
		IdeContext const& context)
	{
		if (context.shared == nullptr)
		{
			return;
		}
		const IdeSelection current = context.shared->selection;
		if (!current.active)
		{
			return;	// no open document is the active editor - nothing to prime
		}
		const JsonValue notification = OrkigeEditor::ideNotification(
			"selection_changed", OrkigeEditor::ideSelectionParams(current));
		connection.sendMessage(notification.serialize());
	}
	//---------------------------------------------------------
	void EditorIdeServer::pushSelectionChange(IdeContext const& context)
	{
		if (context.shared == nullptr)
		{
			return;
		}
		const IdeSelection current = context.shared->selection;
		if (!mSelectionSeeded)
		{
			mLastSelection = current;
			mSelectionSeeded = true;
			return;
		}
		if (!OrkigeEditor::ideSelectionChanged(mLastSelection, current))
		{
			return;
		}
		mLastSelection = current;
		if (mConnections.empty())
		{
			return;
		}
		const JsonValue notification = OrkigeEditor::ideNotification(
			"selection_changed", OrkigeEditor::ideSelectionParams(current));
		const String payload = notification.serialize();
		for (auto& connection : mConnections)
		{
			connection->sendMessage(payload);
		}
	}

	//=========================================================
	//--- EditorIdeSelfTest (a fake IDE client, worker thread)-
	//=========================================================
	namespace
	{
		//! blocking connect to 127.0.0.1:port (INVALID on failure)
		DebugSocketUtil::SocketHandle connectLoopback(unsigned short port)
		{
			DebugSocketUtil::initialise();
			DebugSocketUtil::SocketHandle handle = static_cast<
				DebugSocketUtil::SocketHandle>(
				::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
			if (handle == DebugSocketUtil::INVALID_SOCKET_HANDLE)
			{
				return DebugSocketUtil::INVALID_SOCKET_HANDLE;
			}
			sockaddr_in address;
			std::memset(&address, 0, sizeof(address));
			address.sin_family = AF_INET;
			address.sin_port = htons(port);
			::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
			if (::connect(handle, reinterpret_cast<sockaddr*>(&address),
				sizeof(address)) != 0)
			{
				DebugSocketUtil::closeSocket(handle);
				return DebugSocketUtil::INVALID_SOCKET_HANDLE;
			}
			return handle;
		}

		//! send every byte of data (blocking best-effort)
		bool sendAll(DebugSocketUtil::SocketHandle handle, String const& data)
		{
			std::size_t sent = 0;
			while (sent < data.size())
			{
				const long n = static_cast<long>(::send(handle,
					data.data() + sent, data.size() - sent, 0));
				if (n <= 0)
				{
					return false;
				}
				sent += static_cast<std::size_t>(n);
			}
			return true;
		}

		//! recv until `needle` appears in the accumulator or a deadline passes
		bool recvUntil(DebugSocketUtil::SocketHandle handle, String& acc,
			const char* needle)
		{
			const auto deadline = std::chrono::steady_clock::now() +
				std::chrono::seconds(5);
			while (std::chrono::steady_clock::now() < deadline)
			{
				if (acc.find(needle) != String::npos)
				{
					return true;
				}
				char chunk[2048];
				const long n = static_cast<long>(
					::recv(handle, chunk, sizeof(chunk), 0));
				if (n > 0)
				{
					acc.append(chunk, static_cast<std::size_t>(n));
					continue;
				}
				if (n == 0)
				{
					return acc.find(needle) != String::npos;
				}
				if (!DebugSocketUtil::lastErrorWouldBlock())
				{
					return false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			return acc.find(needle) != String::npos;
		}

		//! recv one complete WebSocket text frame's payload (blocking, bounded)
		bool recvFrame(DebugSocketUtil::SocketHandle handle, String& buffer,
			String& outPayload)
		{
			const auto deadline = std::chrono::steady_clock::now() +
				std::chrono::seconds(5);
			for (;;)
			{
				std::size_t consumed = 0;
				WebSocketUtil::Frame frame;
				const WebSocketUtil::DecodeResult result =
					WebSocketUtil::decodeFrame(buffer, consumed, frame);
				if (result == WebSocketUtil::DecodeResult::Ok)
				{
					buffer.erase(0, consumed);
					if (frame.opcode == WebSocketUtil::OP_TEXT ||
						frame.opcode == WebSocketUtil::OP_BINARY)
					{
						outPayload = frame.payload;
						return true;
					}
					continue;	// skip control frames
				}
				if (result == WebSocketUtil::DecodeResult::Error)
				{
					return false;
				}
				if (std::chrono::steady_clock::now() >= deadline)
				{
					return false;
				}
				char chunk[2048];
				const long n = static_cast<long>(
					::recv(handle, chunk, sizeof(chunk), 0));
				if (n > 0)
				{
					buffer.append(chunk, static_cast<std::size_t>(n));
					continue;
				}
				if (n == 0)
				{
					return false;
				}
				if (!DebugSocketUtil::lastErrorWouldBlock())
				{
					return false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
		}
	}
	//---------------------------------------------------------
	EditorIdeSelfTest::~EditorIdeSelfTest()
	{
		if (mThread.joinable())
		{
			mThread.join();
		}
	}
	//---------------------------------------------------------
	void EditorIdeSelfTest::begin(unsigned short port, std::string const& token,
		std::string const& openFileTarget, std::string const& preOpenTarget)
	{
		mToken = token;
		mOpenFileTarget = openFileTarget;
		mPreOpenTarget = preOpenTarget;
		mActive.store(true);
		mThread = std::thread(&EditorIdeSelfTest::run, this, port);
	}
	//---------------------------------------------------------
	void EditorIdeSelfTest::update()
	{
		// the verdict is produced entirely on the worker thread; nothing to pump
	}
	//---------------------------------------------------------
	std::string EditorIdeSelfTest::openedFile()
	{
		std::lock_guard<std::mutex> guard(mOpenedMutex);
		return mOpenedFile;
	}
	//---------------------------------------------------------
	void EditorIdeSelfTest::run(unsigned short port)
	{
		bool ok = true;
		auto fail = [&](const char* why)
		{
			ok = false;
			oDebugError("editor.ide", 0, "IDE selfcheck failed: " << why);
		};

		// leg 1: a handshake with a BAD token must be refused (401, not 101)
		{
			DebugSocketUtil::SocketHandle bad = connectLoopback(port);
			if (bad != DebugSocketUtil::INVALID_SOCKET_HANDLE)
			{
				String handshake =
					"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
					"Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
					"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
					"x-claude-code-ide-authorization: wrong-token\r\n\r\n";
				sendAll(bad, handshake);
				String reply;
				recvUntil(bad, reply, "\r\n");
				if (reply.find("101") != String::npos)
				{
					fail("a wrong-token handshake was accepted");
				}
				DebugSocketUtil::closeSocket(bad);
			}
		}

		// leg 2: the real conversation over an authorized handshake
		DebugSocketUtil::SocketHandle handle = connectLoopback(port);
		if (handle == DebugSocketUtil::INVALID_SOCKET_HANDLE)
		{
			fail("could not connect to the IDE port");
		}
		String rxBuffer;
		if (ok)
		{
			String handshake =
				"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\n"
				"Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\n"
				"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
				"x-claude-code-ide-authorization: " + String(mToken.c_str()) +
				"\r\n\r\n";
			if (!sendAll(handle, handshake))
			{
				fail("handshake send failed");
			}
			String head;
			if (ok && (!recvUntil(handle, head, "\r\n\r\n") ||
				head.find("101") == String::npos))
			{
				fail("no 101 upgrade for the authorized handshake");
			}
			// any bytes past the header head are the first frame bytes
			const std::size_t bodyStart = head.find("\r\n\r\n");
			if (ok && bodyStart != String::npos)
			{
				rxBuffer = head.substr(bodyStart + 4);
			}
		}

		// server-pushed selection_changed notifications (filePath), collected as
		// they arrive interleaved with tool replies - the initial-state prime and
		// the live delta on a document open both land here without any poll
		std::vector<std::string> selectionFiles;
		auto baseName = [](std::string const& path) -> std::string
		{
			const std::size_t slash = path.find_last_of("/\\");
			return slash == std::string::npos ? path : path.substr(slash + 1);
		};
		// consume one decoded message: a server notification (no id, has method)
		// is recorded and swallowed; anything else is a reply for the caller
		auto consumeNotification = [&](JsonValue const& msg) -> bool
		{
			if (!msg.get("method").isString())
			{
				return false;	// a JSON-RPC reply (id + result/error)
			}
			if (msg.get("method").asString() == "selection_changed")
			{
				selectionFiles.push_back(std::string(
					msg.get("params").get("filePath").asString().c_str()));
			}
			return true;	// a notification - keep reading for the real reply
		};
		auto sawSelection = [&](std::string const& wantBase) -> bool
		{
			for (std::string const& file : selectionFiles)
			{
				if (baseName(file) == wantBase)
				{
					return true;
				}
			}
			return false;
		};
		// block (bounded) until a selection_changed naming wantBase has arrived
		auto awaitSelection = [&](std::string const& wantBase) -> bool
		{
			if (sawSelection(wantBase))
			{
				return true;
			}
			for (int attempt = 0; attempt < 3 && !sawSelection(wantBase); ++attempt)
			{
				String payload;
				if (!recvFrame(handle, rxBuffer, payload))
				{
					break;	// no frame within the recv deadline
				}
				JsonValue msg;
				if (JsonValue::parse(payload, msg))
				{
					consumeNotification(msg);
				}
			}
			return sawSelection(wantBase);
		};

		unsigned int maskSeed = 0x1a2b3c4d;
		auto call = [&](JsonValue const& request, JsonValue& outReply) -> bool
		{
			const String payload = request.serialize();
			maskSeed = maskSeed * 1664525u + 1013904223u;
			const String frame = WebSocketUtil::encodeMaskedFrame(
				WebSocketUtil::OP_TEXT, payload, maskSeed);
			if (!sendAll(handle, frame))
			{
				return false;
			}
			// a pushed notification may arrive before (or interleaved with) the
			// reply to this request; record and skip it, keep reading for the reply
			for (;;)
			{
				String replyPayload;
				if (!recvFrame(handle, rxBuffer, replyPayload))
				{
					return false;
				}
				if (!JsonValue::parse(replyPayload, outReply))
				{
					return false;
				}
				if (consumeNotification(outReply))
				{
					continue;
				}
				return true;
			}
		};
		auto rpc = [](const char* method, JsonValue params, int id) -> JsonValue
		{
			JsonValue r = JsonValue::object();
			r.set("jsonrpc", JsonValue("2.0"));
			r.set("id", JsonValue(static_cast<double>(id)));
			r.set("method", JsonValue(method));
			if (!params.isNull())
			{
				r.set("params", params);
			}
			return r;
		};
		auto toolCall = [&](const char* tool, JsonValue args, int id) -> JsonValue
		{
			JsonValue params = JsonValue::object();
			params.set("name", JsonValue(tool));
			params.set("arguments", args);
			return rpc("tools/call", params, id);
		};

		// initialize
		if (ok)
		{
			JsonValue reply;
			if (!call(rpc("initialize", JsonValue::object(), 1), reply) ||
				!reply.get("result").get("serverInfo").isObject())
			{
				fail("initialize did not return serverInfo");
			}
		}
		// INITIAL-STATE PUSH (open-then-connect): a document the editor opened
		// BEFORE this client connected must reach us as a selection_changed right
		// after initialize, with no poll and no user action - the connect-after-
		// open context the delta stream alone never delivers.
		if (ok && !mPreOpenTarget.empty())
		{
			if (!awaitSelection(baseName(mPreOpenTarget)))
			{
				fail("no initial selection_changed for the pre-opened file");
			}
		}
		// ACTIVE FILE WITHOUT A SELECTION: getCurrentSelection must report the
		// open document as the active file (a real filePath, cursor-only range
		// serialising isEmpty=true) even though no text is selected and the Script
		// panel never held live focus in this headless run.
		if (ok && !mPreOpenTarget.empty())
		{
			JsonValue reply;
			if (!call(toolCall("getCurrentSelection", JsonValue::object(), 6),
				reply))
			{
				fail("getCurrentSelection returned nothing");
			}
			else
			{
				JsonValue body;
				const JsonValue& content = reply.get("result").get("content");
				if (content.size() == 0 ||
					!JsonValue::parse(content.at(0).get("text").asString(), body) ||
					!body.get("success").asBool() ||
					baseName(std::string(
						body.get("filePath").asString().c_str())) !=
						baseName(mPreOpenTarget) ||
					!body.get("selection").get("isEmpty").asBool())
				{
					fail("getCurrentSelection did not report the active file "
						"(cursor-only) for the open document");
				}
			}
		}
		// getWorkspaceFolders: the reply's content text is a JSON body naming
		// the project root
		if (ok)
		{
			JsonValue reply;
			if (!call(toolCall("getWorkspaceFolders", JsonValue::object(), 2),
				reply))
			{
				fail("getWorkspaceFolders returned nothing");
			}
			else
			{
				const JsonValue& content =
					reply.get("result").get("content");
				JsonValue body;
				if (content.size() == 0 ||
					!JsonValue::parse(content.at(0).get("text").asString(),
						body) || !body.get("success").asBool())
				{
					fail("getWorkspaceFolders body was not success");
				}
			}
		}
		// openFile: the request must land in the editor's Script panel
		if (ok && !mOpenFileTarget.empty())
		{
			JsonValue args = JsonValue::object();
			args.set("filePath", JsonValue(String(mOpenFileTarget.c_str())));
			JsonValue reply;
			if (!call(toolCall("openFile", args, 3), reply) ||
				reply.get("result").has("isError"))
			{
				fail("openFile was refused");
			}
			else
			{
				std::lock_guard<std::mutex> guard(mOpenedMutex);
				mOpenedFile = mOpenFileTarget;
			}
		}
		// getDiagnostics round-trip (shape only)
		if (ok)
		{
			JsonValue reply;
			if (!call(toolCall("getDiagnostics", JsonValue::object(), 5), reply))
			{
				fail("getDiagnostics returned nothing");
			}
		}
		// LIVE DELTA PUSH (connect-then-open): the openFile above opened a document
		// WHILE this client was connected; its selection_changed must reach us
		// unsolicited - the owner's exact failing path (open a file mid-session).
		// The push also confirms the panel has applied the (deferred) open, so the
		// getOpenEditors active-tab check below is no longer racing that open.
		if (ok && !mOpenFileTarget.empty())
		{
			if (!awaitSelection(baseName(mOpenFileTarget)))
			{
				fail("no selection_changed after opening a file while connected");
			}
		}
		// getOpenEditors: the just-opened file must be listed AND flagged the
		// active tab (the sticky active-editor the IDE surface reports even with
		// the Script panel unfocused - the connect-then-open active-file context)
		if (ok && !mOpenFileTarget.empty())
		{
			JsonValue reply;
			if (!call(toolCall("getOpenEditors", JsonValue::object(), 4), reply))
			{
				fail("getOpenEditors returned nothing");
			}
			else
			{
				JsonValue body;
				const JsonValue& content = reply.get("result").get("content");
				bool activeMatch = false;
				if (content.size() > 0 && JsonValue::parse(
					content.at(0).get("text").asString(), body))
				{
					const JsonValue& tabs = body.get("tabs");
					for (std::size_t i = 0; i < tabs.size(); ++i)
					{
						const JsonValue& tab = tabs.at(i);
						if (tab.get("isActive").asBool() &&
							baseName(std::string(tab.get("uri").asString().c_str()))
								== baseName(mOpenFileTarget))
						{
							activeMatch = true;
							break;
						}
					}
				}
				if (!activeMatch)
				{
					fail("getOpenEditors did not flag the opened file active");
				}
			}
		}

		if (handle != DebugSocketUtil::INVALID_SOCKET_HANDLE)
		{
			DebugSocketUtil::closeSocket(handle);
		}
		mPassed.store(ok);
		mDone.store(true);
		mActive.store(false);
	}
}
