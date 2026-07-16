// EditorHelpPortal.cpp - Help > "Orkige Help": generate the offline help
// site from the docs corpus (Util/make_help_portal.py, stale-checked) and
// serve it on the editor's dedicated loopback help server (see HelpPortal in
// EditorApp.h for why this is NOT the Play-in-Browser server). The async
// generator pump mirrors the export job (EditorExport.cpp); the static-file
// handler shares the jail + content-type helpers with EditorBrowserServe.cpp.
#include "EditorApp.h"
#include "PythonToolchain.h"

#include <core_debugnet/DebugSocket.h>

#ifdef _WIN32
#	include <winsock2.h>
#	include <ws2tcpip.h>
#else
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <sys/time.h>
#endif

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace
{
	//! @brief spawn the generator: make_help_portal.py --if-stale (an
	//! unchanged corpus is a sub-second no-op that still reports the site
	//! directory, so re-opening Help after a docs pull refreshes the site
	//! automatically)
	bool helpPortalStartGeneration(HelpPortal& portal, EditorConsole& console)
	{
		const Orkige::PythonProbeResult& python =
			Orkige::probePythonToolchain();
		const std::string generator = std::string(ORKIGE_EDITOR_ENGINE_ROOT) +
			"/Util/make_help_portal.py";
		const std::string outputDir =
			std::string(ORKIGE_EDITOR_ENGINE_BUILD_DIR) + "/help_portal";
		const std::vector<std::string> command = { python.executable,
			generator, "--if-stale", "--output", outputDir };
		std::vector<const char*> args;
		std::string commandLine;
		args.reserve(command.size() + 1);
		for (std::string const& arg : command)
		{
			args.push_back(arg.c_str());
			commandLine += (commandLine.empty() ? "" : " ") + arg;
		}
		args.push_back(nullptr);
		SDL_PropertiesID spawnProperties = SDL_CreateProperties();
		SDL_SetPointerProperty(spawnProperties,
			SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
			const_cast<char**>(args.data()));
		SDL_SetNumberProperty(spawnProperties,
			SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
		SDL_SetBooleanProperty(spawnProperties,
			SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
		portal.process = SDL_CreateProcessWithProperties(spawnProperties);
		SDL_DestroyProperties(spawnProperties);
		if (!portal.process)
		{
			console.addLine(ConsoleLevel::Error, "[help] FAILED to run '" +
				commandLine + "': " + SDL_GetError());
			return false;
		}
		portal.outputBuffer.clear();
		portal.artifactPath.clear();
		return true;
	}
}

void helpPortalRequest(HelpPortal& portal, EditorConsole& console)
{
	if (portal.isGenerating())
	{
		console.addLine(ConsoleLevel::Warning,
			"[help] the help site is already being generated - one moment");
		return;
	}
	// preflight the python3 toolchain (cached per run) - report a missing/
	// too-old interpreter honestly instead of letting the spawn fail opaquely
	const Orkige::PythonProbeResult& python = Orkige::probePythonToolchain();
	if (!python.ok)
	{
		console.addLine(ConsoleLevel::Error, "[help] " + python.error);
		return;
	}
	if (!helpPortalStartGeneration(portal, console))
	{
		portal.status = "failed";
		return;
	}
	portal.status = "generating";
}

void helpPortalUpdate(HelpPortal& portal, EditorConsole& console)
{
	if (portal.isGenerating())
	{
		auto emitLine = [&console, &portal](std::string const& text)
		{
			const std::string okPrefix = "make_help_portal: OK ";
			if (text.rfind(okPrefix, 0) == 0)
			{
				portal.artifactPath = text.substr(okPrefix.size());
			}
			const ConsoleLevel level =
				text.find("BROKEN") != std::string::npos ||
				text.find("Error") != std::string::npos ||
				text.find("error") != std::string::npos
					? ConsoleLevel::Error : ConsoleLevel::Info;
			console.addLine(level, "[help] " + text);
		};
		SDL_IOStream* output = SDL_GetProcessOutput(portal.process);
		if (output)
		{
			char chunk[4096];
			size_t bytesRead = 0;
			while ((bytesRead = SDL_ReadIO(output, chunk, sizeof(chunk))) > 0)
			{
				portal.outputBuffer.append(chunk, bytesRead);
			}
		}
		std::size_t lineStart = 0;
		std::size_t newline = std::string::npos;
		while ((newline = portal.outputBuffer.find('\n', lineStart)) !=
			std::string::npos)
		{
			std::string line =
				portal.outputBuffer.substr(lineStart, newline - lineStart);
			if (!line.empty() && line.back() == '\r')
			{
				line.pop_back();
			}
			emitLine(line);
			lineStart = newline + 1;
		}
		portal.outputBuffer.erase(0, lineStart);
		int exitCode = 0;
		if (SDL_WaitProcess(portal.process, false, &exitCode))
		{
			if (!portal.outputBuffer.empty())
			{
				emitLine(portal.outputBuffer);	// drain an unterminated tail
				portal.outputBuffer.clear();
			}
			SDL_DestroyProcess(portal.process);
			portal.process = nullptr;
			if (exitCode != 0 || portal.artifactPath.empty())
			{
				console.addLine(ConsoleLevel::Error,
					"[help] help site generation FAILED (exit " +
					std::to_string(exitCode) + ") - see the lines above");
				portal.status = "failed";
			}
			else
			{
				// serve the fresh site (the listener starts on first use and
				// stays for the editor's lifetime) and open the browser at it
				if (!portal.server.isListening() && !portal.server.start(0))
				{
					console.addLine(ConsoleLevel::Error, "[help] could not "
						"open a loopback listen socket for the help site");
					portal.status = "failed";
				}
				else
				{
					portal.docRoot = portal.artifactPath;
					portal.url = "http://127.0.0.1:" +
						std::to_string(portal.server.getPort()) +
						"/index.html";
					portal.status = "serving";
					console.addLine(ConsoleLevel::Info,
						"[help] serving the help site at " + portal.url);
					// automated runs never touch the user's default browser
					// (the scripted test fetches the served files itself)
					if (!gAutomatedRun && !SDL_OpenURL(portal.url.c_str()))
					{
						console.addLine(ConsoleLevel::Error,
							"[help] could not open the default browser: " +
							std::string(SDL_GetError()) + " - open " +
							portal.url + " yourself");
					}
				}
			}
		}
	}
	if (!portal.server.isListening())
	{
		return;
	}
	// static GETs only - the help server hosts no protocol upgrades and no
	// mutations (an upgrade request is just a GET here and receives a page)
	portal.server.update([&portal](
		Orkige::HttpRequest const& request) -> Orkige::HttpResponse
	{
		Orkige::HttpResponse response;
		if (request.method != "GET")
		{
			response.status = 405;
			response.reason = "Method Not Allowed";
			response.contentType = "text/plain";
			response.body = "help portal: GET only\n";
			return response;
		}
		const std::string filePath = portal.docRoot.empty()
			? std::string()
			: staticResolveServedFile(portal.docRoot, request.target);
		if (filePath.empty())
		{
			response.status = 404;
			response.reason = "Not Found";
			response.contentType = "text/plain";
			response.body = "no such file in the help site\n";
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

//--- ORKIGE_EDITOR_HELPTEST self-test (editor_help_portal ctest) ------------

namespace
{
	//! blocking loopback HTTP GET against the pumped help server: connect,
	//! send, read one response (status + headers + Content-Length body).
	//! Worker-thread test plumbing mirroring the control self-test's raw
	//! clients (EditorControlServer.cpp); the recv timeout is a coarse
	//! anti-hang backstop, not a latency budget - the reply arrives whenever
	//! the main thread pumps the server next.
	bool helpTestGet(unsigned short port, std::string const& target,
		int& status, std::string& headers, std::string& body)
	{
		Orkige::DebugSocketUtil::initialise();
		Orkige::DebugSocketUtil::SocketHandle handle =
			Orkige::DebugSocketUtil::INVALID_SOCKET_HANDLE;
		for (int attempt = 0; attempt < 20; ++attempt)
		{
			handle = static_cast<Orkige::DebugSocketUtil::SocketHandle>(
				::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
			if (handle == Orkige::DebugSocketUtil::INVALID_SOCKET_HANDLE)
			{
				return false;
			}
#ifdef _WIN32
			DWORD timeoutMs = 45000;
			::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO,
				reinterpret_cast<char*>(&timeoutMs), sizeof(timeoutMs));
#else
			struct timeval tv;
			tv.tv_sec = 45;
			tv.tv_usec = 0;
			::setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
			struct sockaddr_in address;
			std::memset(&address, 0, sizeof(address));
			address.sin_family = AF_INET;
			address.sin_port = htons(port);
			::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
			if (::connect(handle,
				reinterpret_cast<struct sockaddr*>(&address),
				sizeof(address)) == 0)
			{
				break;
			}
			Orkige::DebugSocketUtil::closeSocket(handle);
			handle = Orkige::DebugSocketUtil::INVALID_SOCKET_HANDLE;
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		if (handle == Orkige::DebugSocketUtil::INVALID_SOCKET_HANDLE)
		{
			return false;
		}
		const std::string request = "GET " + target + " HTTP/1.1\r\n"
			"Host: 127.0.0.1\r\nConnection: close\r\n\r\n";
		size_t sent = 0;
		bool sendOk = true;
		while (sent < request.size())
		{
			const long n = static_cast<long>(::send(handle,
				request.data() + sent, request.size() - sent, 0));
			if (n <= 0)
			{
				sendOk = false;
				break;
			}
			sent += static_cast<size_t>(n);
		}
		bool ok = false;
		if (sendOk)
		{
			std::string buffer;
			char chunk[4096];
			size_t headerEnd = std::string::npos;
			while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos)
			{
				const long n = static_cast<long>(::recv(handle, chunk,
					sizeof(chunk), 0));
				if (n <= 0 || buffer.size() > 8 * 1024 * 1024)
				{
					break;
				}
				buffer.append(chunk, static_cast<size_t>(n));
			}
			if (headerEnd != std::string::npos)
			{
				headers = buffer.substr(0, headerEnd);
				const size_t space = headers.find(' ');
				status = space == std::string::npos
					? 0 : std::atoi(headers.c_str() + space + 1);
				size_t contentLength = 0;
				std::string lower = headers;
				for (char& c : lower)
				{
					c = static_cast<char>(std::tolower(
						static_cast<unsigned char>(c)));
				}
				const size_t lengthAt = lower.find("content-length:");
				if (lengthAt != std::string::npos)
				{
					contentLength = static_cast<size_t>(std::strtoul(
						headers.c_str() + lengthAt +
						std::strlen("content-length:"), nullptr, 10));
				}
				const size_t bodyStart = headerEnd + 4;
				ok = true;
				while (buffer.size() < bodyStart + contentLength)
				{
					const long n = static_cast<long>(::recv(handle, chunk,
						sizeof(chunk), 0));
					if (n <= 0)
					{
						ok = false;
						break;
					}
					buffer.append(chunk, static_cast<size_t>(n));
				}
				if (ok)
				{
					body = buffer.substr(bodyStart, contentLength);
				}
			}
		}
		Orkige::DebugSocketUtil::closeSocket(handle);
		return ok;
	}
}

HelpPortalSelfTest::~HelpPortalSelfTest()
{
	if (this->mThread.joinable())
	{
		this->mThread.join();
	}
}

void HelpPortalSelfTest::begin(unsigned short port)
{
	this->mActive.store(true);
	this->mDone.store(false);
	this->mPassed.store(false);
	this->mThread = std::thread(&HelpPortalSelfTest::run, this, port);
}

void HelpPortalSelfTest::finish()
{
	if (this->mThread.joinable())
	{
		this->mThread.join();
	}
}

void HelpPortalSelfTest::run(unsigned short port)
{
	bool passed = true;
	auto check = [&passed](bool condition, const char* what)
	{
		SDL_Log("orkige_editor: help-test %s: %s", what,
			condition ? "ok" : "FAILED");
		if (!condition)
		{
			passed = false;
		}
	};
	int status = 0;
	std::string headers;
	std::string body;
	// the portal directory page
	check(helpTestGet(port, "/index.html", status, headers, body) &&
		status == 200, "GET /index.html answers 200");
	check(headers.find("text/html") != std::string::npos,
		"index is served as text/html");
	check(body.find("Orkige Help") != std::string::npos,
		"index carries the portal title");
	check(body.find("id=\"search\"") != std::string::npos,
		"index carries the search box");
	// a rendered corpus page with its heading anchors
	check(helpTestGet(port, "/getting-started.html", status, headers, body) &&
		status == 200, "GET a rendered doc page answers 200");
	check(body.find("<h1 id=") != std::string::npos,
		"the doc page carries anchored headings");
	// the search index the page's JS fetches
	check(helpTestGet(port, "/search-index.json", status, headers, body) &&
		status == 200, "GET /search-index.json answers 200");
	check(headers.find("application/json") != std::string::npos,
		"search index is served as application/json");
	check(body.find("\"anchor\"") != std::string::npos,
		"search index carries section records");
	// the self-contained assets
	check(helpTestGet(port, "/help.js", status, headers, body) &&
		status == 200, "GET /help.js answers 200");
	check(helpTestGet(port, "/help.css", status, headers, body) &&
		status == 200 && headers.find("text/css") != std::string::npos,
		"GET /help.css answers 200 as text/css");
	// the path jail: an escape and a miss both answer 404
	check(helpTestGet(port, "/../CMakeLists.txt", status, headers, body) &&
		status == 404, "a path escape answers 404");
	check(helpTestGet(port, "/no-such-page.html", status, headers, body) &&
		status == 404, "a missing page answers 404");
	this->mPassed.store(passed);
	this->mDone.store(true);
	this->mActive.store(false);
}
