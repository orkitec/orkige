/**************************************************************
	created:	2026/07/24 at 10:00
	filename: 	script_debug_driver.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
//
// player_script_debug integration driver: spawns a real orkige_player on the
// jumper-lua project, connects over the ONE debug protocol and verifies the
// script DEBUGGER contract end to end:
//
//   1. hello arrives; the breakpoint set is applied (MSG_DEBUG_BREAKPOINTS,
//      full-list replace) on the first executable line of update() in
//      scripts/player.lua (located by scanning the script, so the test never
//      hardcodes a line number)
//   2. MSG_DEBUG_BREAK arrives with the correct file + line and parallel
//      stack lists (innermost frame = the paused location)
//   3. MSG_DEBUG_LOCALS at frame 0 lists update's parameters (self, dt);
//      an expand request on 'self' lists its fields (scope "field")
//   4. a non-debug command sent WHILE BROKEN (request_hierarchy) is
//      deferred, not lost: the hierarchy arrives after the resume
//   5. step over: MSG_DEBUG_RESUMED, then a fresh MSG_DEBUG_BREAK on the
//      NEXT line
//   6. continue with the breakpoint still set re-hits on a later frame
//   7. clearing the set + resume lets the game run free (stats keep
//      streaming, no further breaks)
//   7b/7c. BREAK ON NEXT STATEMENT (MSG_DEBUG_BREAK_NEXT): with NO breakpoint
//      set, arming it while RUNNING pauses on the next executed line (real
//      file/line + stack); arming it while FRAME-PAUSED persists (no break
//      fires) and the first line after the sim resumes catches it
//   8. a client that DISCONNECTS mid-break must not wedge the player: a
//      fresh connection gets hello + streams, and no stale break holds
//   9. quit shuts the player down with exit code 0
//
// Usage: script_debug_driver <orkige_player> <projectDir>
// Exit code 0 = all assertions held. Registered in tests/CMakeLists.txt as
// player_script_debug (both flavors; LABELS integration).

#include <core_debugnet/DebugClient.h>
#include <core_debugnet/DebugServer.h>
#include <core_debugnet/DebugProtocol.h>

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace Protocol = Orkige::DebugProtocol;

namespace
{
	pid_t playerPid = -1;

	void log(std::string const & text)
	{
		std::fprintf(stderr, "script_debug_driver: %s\n", text.c_str());
		std::fflush(stderr);
	}

	int fail(std::string const & reason)
	{
		log("FAILED - " + reason);
		if (playerPid > 0)
		{
			::kill(playerPid, SIGKILL);
			int status = 0;
			::waitpid(playerPid, &status, 0);
		}
		return 1;
	}

	std::chrono::steady_clock::time_point deadlineIn(int milliseconds)
	{
		return std::chrono::steady_clock::now() +
			std::chrono::milliseconds(milliseconds);
	}

	//! wait (pumping) for the next message of the given type; false on timeout
	bool waitForMessage(Orkige::DebugClient & client,
		Orkige::String const & type, Orkige::DebugMessage & out,
		int timeoutMilliseconds)
	{
		const std::chrono::steady_clock::time_point deadline =
			deadlineIn(timeoutMilliseconds);
		while (std::chrono::steady_clock::now() < deadline)
		{
			client.update();
			Orkige::DebugMessage message;
			while (client.receive(message))
			{
				if (message.type == type)
				{
					out = message;
					return true;
				}
			}
			if (!client.isConnected())
			{
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return false;
	}

	//! assert that NO message of the given type arrives within the window
	bool absentForWindow(Orkige::DebugClient & client,
		Orkige::String const & type, int windowMilliseconds)
	{
		const std::chrono::steady_clock::time_point deadline =
			deadlineIn(windowMilliseconds);
		while (std::chrono::steady_clock::now() < deadline)
		{
			client.update();
			Orkige::DebugMessage message;
			while (client.receive(message))
			{
				if (message.type == type)
				{
					return false;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return true;
	}

	//! connect (with retries) to the player's debug port
	bool connectWithRetries(Orkige::DebugClient & client, unsigned short port,
		int timeoutMilliseconds)
	{
		const std::chrono::steady_clock::time_point deadline =
			deadlineIn(timeoutMilliseconds);
		while (!client.isConnected())
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}
			if (playerPid > 0)
			{
				int status = 0;
				if (::waitpid(playerPid, &status, WNOHANG) == playerPid)
				{
					playerPid = -1;
					return false;
				}
			}
			client.connect("127.0.0.1", port);
			const std::chrono::steady_clock::time_point attemptEnd =
				deadlineIn(250);
			while (client.isConnecting() &&
				std::chrono::steady_clock::now() < attemptEnd)
			{
				client.update();
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			if (!client.isConnected())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
		}
		return true;
	}

	//! the 1-based line of update()'s FIRST STATEMENT in the given script -
	//! located by scanning, so a jumper-lua edit moves the breakpoint along
	int findUpdateBodyLine(std::string const & scriptPath)
	{
		std::ifstream file(scriptPath);
		std::string line;
		int lineNumber = 0;
		while (std::getline(file, line))
		{
			++lineNumber;
			if (line.find("function update(self, dt)") != std::string::npos)
			{
				return lineNumber + 1;
			}
		}
		return 0;
	}
}

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		std::fprintf(stderr,
			"usage: script_debug_driver <orkige_player> <projectDir>\n");
		return 2;
	}
	const std::string playerBinary = argv[1];
	const std::string projectDir = argv[2];
	const std::string scriptFile = "scripts/player.lua";

	const int breakLine =
		findUpdateBodyLine(projectDir + "/" + scriptFile);
	if (breakLine <= 1)
	{
		return fail("could not locate update() in " + scriptFile);
	}
	log("breakpoint target: " + scriptFile + ":" + std::to_string(breakLine));

	// a free localhost port (same probe idiom as player_debug_driver)
	unsigned short port = 0;
	{
		Orkige::DebugServer portProbe;
		if (!portProbe.start(0))
		{
			return fail("could not probe for a free port");
		}
		port = portProbe.getPort();
	}
	const std::string portString = std::to_string(port);
	const char* args[] = { playerBinary.c_str(),
		"--project", projectDir.c_str(),
		"--debug-port", portString.c_str(), nullptr };
	if (::posix_spawn(&playerPid, playerBinary.c_str(), nullptr, nullptr,
		const_cast<char* const*>(args), environ) != 0)
	{
		return fail("posix_spawn '" + playerBinary + "' failed");
	}
	log("spawned player pid " + std::to_string(playerPid) + " on port " +
		portString);

	Orkige::DebugClient client;
	if (!connectWithRetries(client, port, 30000))
	{
		return fail("could not connect to the player within 30s");
	}
	Orkige::DebugMessage message;
	if (!waitForMessage(client, Protocol::MSG_HELLO, message, 30000))
	{
		return fail("no hello");
	}
	log("connected + hello");

	// 1. apply the breakpoint set (full-list replace)
	{
		Orkige::DebugMessage breakpoints(Protocol::MSG_DEBUG_BREAKPOINTS);
		breakpoints.setList(Protocol::LIST_BREAKPOINTS,
			{ scriptFile + ":" + std::to_string(breakLine) });
		client.send(breakpoints);
	}

	// 2. the break-hit notification (the scripts load lazily; generous
	// window - the first run may pay shader-generation cost)
	if (!waitForMessage(client, Protocol::MSG_DEBUG_BREAK, message, 30000))
	{
		return fail("no debug_break after setting the breakpoint");
	}
	if (message.get(Protocol::FIELD_PATH) != scriptFile)
	{
		return fail("break file '" + message.get(Protocol::FIELD_PATH) +
			"' (expected '" + scriptFile + "')");
	}
	if (message.get(Protocol::FIELD_LINE) != std::to_string(breakLine))
	{
		return fail("break line " + message.get(Protocol::FIELD_LINE) +
			" (expected " + std::to_string(breakLine) + ")");
	}
	{
		Orkige::StringVector const & sources =
			message.getList(Protocol::LIST_STACK_SOURCES);
		Orkige::StringVector const & lines =
			message.getList(Protocol::LIST_STACK_LINES);
		Orkige::StringVector const & functions =
			message.getList(Protocol::LIST_STACK_FUNCTIONS);
		if (sources.empty() || sources.size() != lines.size() ||
			sources.size() != functions.size())
		{
			return fail("break stack lists empty or not parallel");
		}
		if (sources[0] != scriptFile ||
			lines[0] != std::to_string(breakLine))
		{
			return fail("innermost stack frame is not the paused location");
		}
	}
	log("breakpoint hit with a consistent stack");

	// 3. locals at frame 0: update's parameters must be visible
	{
		Orkige::DebugMessage request(Protocol::MSG_DEBUG_LOCALS);
		request.set(Protocol::FIELD_FRAME, "0");
		client.send(request);
		if (!waitForMessage(client, Protocol::MSG_DEBUG_LOCALS, message,
			10000))
		{
			return fail("no debug_locals reply");
		}
		Orkige::StringVector const & names =
			message.getList(Protocol::LIST_VAR_NAMES);
		Orkige::StringVector const & values =
			message.getList(Protocol::LIST_VAR_VALUES);
		if (names.size() != values.size() || names.empty())
		{
			return fail("locals lists empty or not parallel");
		}
		bool sawSelf = false;
		bool sawDt = false;
		for (std::size_t i = 0; i < names.size(); ++i)
		{
			if (names[i] == "self")
			{
				sawSelf = true;
			}
			if (names[i] == "dt")
			{
				sawDt = true;
			}
		}
		if (!sawSelf || !sawDt)
		{
			return fail("locals of update() did not include self and dt");
		}
		// the explicit expand request lists self's fields
		Orkige::DebugMessage expand(Protocol::MSG_DEBUG_LOCALS);
		expand.set(Protocol::FIELD_FRAME, "0");
		expand.setList(Protocol::LIST_EXPAND_PATH, { "self" });
		client.send(expand);
		if (!waitForMessage(client, Protocol::MSG_DEBUG_LOCALS, message,
			10000))
		{
			return fail("no expand reply for 'self'");
		}
		Orkige::StringVector const & fieldNames =
			message.getList(Protocol::LIST_VAR_NAMES);
		Orkige::StringVector const & scopes =
			message.getList(Protocol::LIST_VAR_SCOPES);
		if (fieldNames.empty())
		{
			return fail("'self' expanded to no fields");
		}
		bool sawId = false;
		for (std::size_t i = 0; i < fieldNames.size(); ++i)
		{
			if (i < scopes.size() && scopes[i] != "field")
			{
				return fail("an expanded row's scope was not 'field'");
			}
			if (fieldNames[i] == "id")
			{
				sawId = true;
			}
		}
		if (!sawId)
		{
			return fail("self's fields did not include 'id'");
		}
	}
	log("locals + expansion OK (self, dt; self.id)");

	// 4. a non-debug command while broken is DEFERRED, never lost
	client.send(Orkige::DebugMessage(Protocol::MSG_REQUEST_HIERARCHY));

	// 5. step over -> resumed + a fresh break on the NEXT line
	client.send(Orkige::DebugMessage(Protocol::MSG_DEBUG_STEP_OVER));
	if (!waitForMessage(client, Protocol::MSG_DEBUG_RESUMED, message, 10000))
	{
		return fail("no debug_resumed after step over");
	}
	if (!waitForMessage(client, Protocol::MSG_DEBUG_BREAK, message, 10000))
	{
		return fail("the step never landed");
	}
	if (message.get(Protocol::FIELD_LINE) !=
		std::to_string(breakLine + 1))
	{
		return fail("step over landed on line " +
			message.get(Protocol::FIELD_LINE) + " (expected " +
			std::to_string(breakLine + 1) + ")");
	}
	log("step over landed on the next line");

	// 6. continue with the breakpoint still set: re-hits on a later frame
	client.send(Orkige::DebugMessage(Protocol::MSG_DEBUG_RESUME));
	if (!waitForMessage(client, Protocol::MSG_DEBUG_RESUMED, message, 10000))
	{
		return fail("no debug_resumed after continue");
	}
	// the deferred request_hierarchy from step 4 must surface now
	if (!waitForMessage(client, Protocol::MSG_HIERARCHY, message, 10000))
	{
		return fail("the mid-break request_hierarchy was lost (no hierarchy "
			"after resume)");
	}
	log("mid-break command was deferred and answered after resume");
	if (!waitForMessage(client, Protocol::MSG_DEBUG_BREAK, message, 10000))
	{
		return fail("the breakpoint did not re-hit after continue");
	}
	if (message.get(Protocol::FIELD_LINE) != std::to_string(breakLine))
	{
		return fail("the re-hit was not on the breakpoint line");
	}
	log("breakpoint re-hit after continue");

	// 7. clear the whole set + resume: the game runs free again
	{
		Orkige::DebugMessage clearAll(Protocol::MSG_DEBUG_BREAKPOINTS);
		clearAll.setList(Protocol::LIST_BREAKPOINTS, {});
		client.send(clearAll);
		client.send(Orkige::DebugMessage(Protocol::MSG_DEBUG_RESUME));
		if (!waitForMessage(client, Protocol::MSG_DEBUG_RESUMED, message,
			10000))
		{
			return fail("no debug_resumed after the final resume");
		}
		// stats keep streaming = the game is alive and free-running
		if (!waitForMessage(client, Protocol::MSG_STATS, message, 15000))
		{
			return fail("no stats stream after clearing the breakpoints");
		}
		if (!absentForWindow(client, Protocol::MSG_DEBUG_BREAK, 700))
		{
			return fail("a break arrived after the set was cleared");
		}
	}
	log("cleared set runs free");

	// 7b. BREAK ON NEXT STATEMENT while RUNNING: no breakpoint set - arm
	// break-next and the very next script line the game runs must pause, with a
	// real file/line + stack, exactly like a breakpoint hit
	{
		client.send(Orkige::DebugMessage(Protocol::MSG_DEBUG_BREAK_NEXT));
		if (!waitForMessage(client, Protocol::MSG_DEBUG_BREAK, message, 15000))
		{
			return fail("break_next never paused while running");
		}
		if (message.get(Protocol::FIELD_PATH).empty() ||
			message.get(Protocol::FIELD_LINE) == "0" ||
			message.getList(Protocol::LIST_STACK_SOURCES).empty())
		{
			return fail("break_next paused without a real location/stack");
		}
		log("break_next (running) paused at " +
			message.get(Protocol::FIELD_PATH) + ":" +
			message.get(Protocol::FIELD_LINE));
		client.send(Orkige::DebugMessage(Protocol::MSG_DEBUG_RESUME));
		if (!waitForMessage(client, Protocol::MSG_DEBUG_RESUMED, message,
			10000))
		{
			return fail("no debug_resumed after the running break_next");
		}
	}

	// 7c. BREAK ON NEXT STATEMENT armed while FRAME-PAUSED: the arm PERSISTS (no
	// script line runs while paused, so no break may fire yet); the first line
	// after the sim resumes catches it
	{
		client.send(Orkige::DebugMessage(Protocol::MSG_PAUSE));
		// let the pause take effect (the player consumes it within a frame),
		// then drain anything buffered so the absence window below is clean
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		client.update();
		{
			Orkige::DebugMessage drain;
			while (client.receive(drain)) {}
		}
		client.send(Orkige::DebugMessage(Protocol::MSG_DEBUG_BREAK_NEXT));
		// paused: scripts do not tick, so NO break may arrive while the arm waits
		if (!absentForWindow(client, Protocol::MSG_DEBUG_BREAK, 700))
		{
			return fail("break_next fired while frame-paused (scripts should "
				"not tick under a frame pause)");
		}
		// resume the sim: the first script line now catches the armed break
		client.send(Orkige::DebugMessage(Protocol::MSG_RESUME));
		if (!waitForMessage(client, Protocol::MSG_DEBUG_BREAK, message, 15000))
		{
			return fail("break_next never paused after resuming from a frame "
				"pause (the arm did not persist)");
		}
		log("break_next (armed while paused) caught the first line after "
			"resume at " + message.get(Protocol::FIELD_PATH) + ":" +
			message.get(Protocol::FIELD_LINE));
		client.send(Orkige::DebugMessage(Protocol::MSG_DEBUG_RESUME));
		if (!waitForMessage(client, Protocol::MSG_DEBUG_RESUMED, message,
			10000))
		{
			return fail("no debug_resumed after the paused break_next");
		}
	}

	// 8. disconnect-while-broken auto-resumes (never a wedged player): re-set
	// the breakpoint, wait for the hit, then drop the socket abruptly
	{
		Orkige::DebugMessage breakpoints(Protocol::MSG_DEBUG_BREAKPOINTS);
		breakpoints.setList(Protocol::LIST_BREAKPOINTS,
			{ scriptFile + ":" + std::to_string(breakLine) });
		client.send(breakpoints);
		if (!waitForMessage(client, Protocol::MSG_DEBUG_BREAK, message,
			15000))
		{
			return fail("no break for the disconnect leg");
		}
		client.disconnect();
		log("disconnected mid-break");
	}
	// a fresh session must find a live, free-running player (its breakpoints
	// cleared with the vanished client). A reconnect can LAND before the
	// player noticed the loss - the single-client server then refuses it -
	// so retry the whole connect+hello handshake until it sticks.
	{
		Orkige::DebugClient second;
		bool helloSeen = false;
		const std::chrono::steady_clock::time_point handshakeDeadline =
			deadlineIn(20000);
		while (!helloSeen &&
			std::chrono::steady_clock::now() < handshakeDeadline)
		{
			if (!connectWithRetries(second, port, 15000))
			{
				return fail("could not reconnect after the mid-break "
					"disconnect (player wedged?)");
			}
			helloSeen = waitForMessage(second, Protocol::MSG_HELLO, message,
				3000);
			if (!helloSeen)
			{
				second.disconnect();
				std::this_thread::sleep_for(std::chrono::milliseconds(200));
			}
		}
		if (!helloSeen)
		{
			return fail("no hello on the second session");
		}
		if (!waitForMessage(second, Protocol::MSG_STATS, message, 15000))
		{
			return fail("no stats on the second session (player not running "
				"free)");
		}
		if (!absentForWindow(second, Protocol::MSG_DEBUG_BREAK, 700))
		{
			return fail("a stale break hit the second session (the vanished "
				"client's breakpoints were not cleared)");
		}
		log("second session finds a free-running player");

		// 9. quit: clean exit
		second.send(Orkige::DebugMessage(Protocol::MSG_QUIT));
		if (!waitForMessage(second, Protocol::MSG_BYE, message, 5000))
		{
			log("note: no bye before the connection dropped (acceptable)");
		}
	}
	{
		const std::chrono::steady_clock::time_point deadline =
			deadlineIn(10000);
		int status = 0;
		for (;;)
		{
			const pid_t waited = ::waitpid(playerPid, &status, WNOHANG);
			if (waited == playerPid)
			{
				playerPid = -1;
				break;
			}
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return fail("player did not exit within 10s of quit");
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		{
			return fail("player exit status not clean (status " +
				std::to_string(status) + ")");
		}
	}
	log("PASSED - the full script-debugger contract holds "
		"(break/stack/locals/step/re-hit/defer/clear/disconnect/quit)");
	return 0;
}
