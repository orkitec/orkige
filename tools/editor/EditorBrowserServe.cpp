// EditorBrowserServe.cpp - the Play-in-Browser static-file server: a second
// loopback instance of core_debugnet's HttpServer serving one exported web
// build directory (see BrowserServe in EditorApp.h). Split out of main.cpp
// (mechanical decomposition, see EditorApp.h).
//
// The same port is the browser debug transport's front door: the served
// page dials back with a WebSocket upgrade (its POSIX-socket emulation
// wraps the plain debug dial), and while a browser play session is waiting
// for its page the upgrade is answered and the socket handed to that
// session's DebugClient - the ONE debug protocol, WebSocket-framed. This
// instance stays loopback-only and entirely separate from the opt-in MCP
// endpoint, which never answers upgrades.
#include "EditorApp.h"

#include <core_debugnet/WebSocket.h>

#include <filesystem>
#include <fstream>
#include <sstream>

//! the content type a browser needs per served-file extension - shared with
//! the help-portal server (EditorHelpPortal.cpp). wasm MUST be
//! application/wasm exactly - WebAssembly streaming compilation refuses
//! anything else. Everything a web export or the help site contains is
//! covered; an unknown extension degrades to the honest binary default.
std::string staticContentTypeFor(std::string const& path)
{
	const std::size_t dot = path.rfind('.');
	const std::string ext =
		dot == std::string::npos ? "" : path.substr(dot);
	if (ext == ".html") return "text/html; charset=utf-8";
	if (ext == ".js") return "text/javascript";
	if (ext == ".css") return "text/css";
	if (ext == ".wasm") return "application/wasm";
	if (ext == ".png") return "image/png";
	if (ext == ".svg") return "image/svg+xml";
	if (ext == ".json") return "application/json";
	return "application/octet-stream";	// .data payload image et al
}

//! @brief resolve a request target to a real file inside docRoot, or ""
//! when it escapes/misses - shared with the help-portal server. The jail is
//! the same discipline as the control server's project-file verbs: the
//! canonicalised path must stay under the canonicalised root. Requests for
//! files of a PREVIOUS export (the doc root swaps per play) land here as a
//! plain miss -> 404.
std::string staticResolveServedFile(std::string const& docRoot,
	std::string const& target)
{
	namespace fs = std::filesystem;
	// strip the query string ("?env.NAME=VALUE" automation params)
	std::string path = target.substr(0, target.find('?'));
	if (path.empty() || path[0] != '/' ||
		path.find("..") != std::string::npos ||
		path.find('%') != std::string::npos)
	{
		return "";
	}
	if (path == "/")
	{
		path = "/index.html";
	}
	std::error_code ignored;
	const fs::path root = fs::weakly_canonical(docRoot, ignored);
	const fs::path resolved =
		fs::weakly_canonical(root / path.substr(1), ignored);
	const std::string rootString = root.string();
	const std::string resolvedString = resolved.string();
	if (resolvedString.size() <= rootString.size() ||
		resolvedString.compare(0, rootString.size(), rootString) != 0)
	{
		return "";
	}
	if (!fs::is_regular_file(resolved, ignored))
	{
		return "";
	}
	return resolvedString;
}

bool browserServeStart(BrowserServe& serve, std::string const& docRoot,
	std::string& outUrl, std::string& outError)
{
	if (!serve.server.isListening())
	{
		// HttpServer binds 127.0.0.1 only (its contract) on an ephemeral port
		if (!serve.server.start(0))
		{
			outError = "could not open a loopback listen socket";
			return false;
		}
	}
	serve.docRoot = docRoot;
	outUrl = "http://127.0.0.1:" +
		std::to_string(serve.server.getPort()) + "/index.html";
	return true;
}

void browserServeUpdate(BrowserServe& serve, PlaySession& session)
{
	if (!serve.server.isListening())
	{
		return;
	}
	// an accepted debug upgrade lands the socket in THIS session's client
	// (the same DebugClient a desktop play session dials out with - every
	// remote panel/verb above it works unchanged)
	serve.server.setTakeoverHandler(
		[&session](Orkige::DebugSocketUtil::SocketHandle handle,
			std::string const& leftover)
	{
		session.client.adoptWebSocket(handle, leftover);
	});
	// exactly one page may attach, and only while a browser play session
	// is waiting for it; everything else is refused and runs standalone
	const bool acceptDebugUpgrade =
		session.mode == PlaySession::Mode::Launching && session.onBrowser &&
		!session.client.isConnected();
	serve.server.update([&serve, acceptDebugUpgrade](
		Orkige::HttpRequest const& request) -> Orkige::HttpResponse
	{
		Orkige::HttpResponse response;
		if (Orkige::WebSocketUtil::isUpgradeRequest(request))
		{
			if (acceptDebugUpgrade)
			{
				return Orkige::WebSocketUtil::buildHandshakeResponse(request);
			}
			// no session waiting (second tab / reloaded page / play over):
			// the page's dial fails and it continues standalone
			response.status = 409;
			response.reason = "Conflict";
			response.contentType = "text/plain";
			response.body = "no browser play session is waiting for a "
				"debug connection\n";
			return response;
		}
		if (request.method != "GET")
		{
			// GET only - the transport frames every response by the body's
			// Content-Length, which a HEAD reply must omit by definition
			response.status = 405;
			response.reason = "Method Not Allowed";
			response.contentType = "text/plain";
			response.body = "static file server: GET only\n";
			return response;
		}
		const std::string filePath = serve.isServing()
			? staticResolveServedFile(serve.docRoot, request.target)
			: std::string();
		if (filePath.empty())
		{
			response.status = 404;
			response.reason = "Not Found";
			response.contentType = "text/plain";
			response.body = "no such file in the served web build\n";
			return response;
		}
		std::ifstream file(filePath, std::ios::binary);
		std::ostringstream bytes;
		bytes << file.rdbuf();
		if (!file)
		{
			response.status = 500;
			response.reason = "Internal Server Error";
			response.contentType = "text/plain";
			response.body = "file read failed\n";
			return response;
		}
		response.contentType = staticContentTypeFor(filePath);
		response.body = bytes.str();
		return response;
	});
}
