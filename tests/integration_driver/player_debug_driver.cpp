/**************************************************************
	created:	2026/07/07 at 23:30
	filename: 	player_debug_driver.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
//
// player_debug_pause integration driver: spawns a real orkige_player on a
// scene with a falling RigidBody cube, connects over the debug protocol and
// verifies the remote-control contract end to end:
//
//   1. hello + hierarchy arrive after connect
//   2. select streams object_state for the cube
//   3. the cube is falling (y drops) while running
//   4. pause freezes it: two samples 500ms apart are EXACTLY equal
//   5. resume lets it fall again (y drops)
//   6. step while paused advances EXACTLY one tick: y drops once, then
//      holds perfectly still again
//   7. set_property teleports the paused cube (TransformComponent.position)
//   8. an unknown property answers with an error message, link stays alive
//   9. quit shuts the player down with exit code 0
//
// Usage: player_debug_driver <orkige_player binary> <scene.oscene>
// Exit code 0 = all assertions held; anything else = failure (the player
// child is killed on every failure path). Registered in tests/CMakeLists.txt
// as the player_debug_pause ctest test (LABELS integration).

#include <core_debugnet/DebugClient.h>
#include <core_debugnet/DebugServer.h>
#include <core_debugnet/DebugProtocol.h>

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>

extern char** environ;

namespace Protocol = Orkige::DebugProtocol;

namespace
{
	pid_t playerPid = -1;

	void log(std::string const & text)
	{
		std::fprintf(stderr, "player_debug_driver: %s\n", text.c_str());
		std::fflush(stderr);
	}

	//! kill the player (if running) and fail the test run
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

	//! wait (pumping) for the next message of the given type; other message
	//! types are collected or discarded; false on timeout
	bool waitForMessage(Orkige::DebugClient & client, Orkige::String const & type,
		Orkige::DebugMessage & out, int timeoutMilliseconds)
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

	//! parse the y component out of an object_state's
	//! "TransformComponent.position" field ("x y z")
	bool positionY(Orkige::DebugMessage const & state, double & outY)
	{
		std::istringstream stream(state.get("TransformComponent.position"));
		double x = 0.0;
		return static_cast<bool>(stream >> x >> outY);
	}

	//! pull one named object's local-transform y (index 1 of the 10-float
	//! "px py pz qw qx qy qz sx sy sz" string) out of a MSG_SCENE_TRANSFORMS
	//! message (parallel LIST_IDS / LIST_TRANSFORMS); false when the id is
	//! absent from this delta (it did not move this tick)
	bool sceneTransformY(Orkige::DebugMessage const & message,
		Orkige::String const & id, double & outY)
	{
		Orkige::StringVector const & ids =
			message.getList(Protocol::LIST_IDS);
		Orkige::StringVector const & transforms =
			message.getList(Protocol::LIST_TRANSFORMS);
		for (std::size_t i = 0; i < ids.size() && i < transforms.size(); ++i)
		{
			if (ids[i] != id)
			{
				continue;
			}
			std::istringstream stream(transforms[i]);
			double x = 0.0;
			return static_cast<bool>(stream >> x >> outY);
		}
		return false;
	}

	//! discard everything buffered/inflight for the given settle time, then
	//! return the y of the NEXT fresh object_state (streamed at ~15Hz);
	//! the settle window guarantees no pre-command state leaks into the sample
	bool sampleFreshY(Orkige::DebugClient & client, double & outY,
		int settleMilliseconds = 250, int timeoutMilliseconds = 5000)
	{
		const std::chrono::steady_clock::time_point settleEnd =
			deadlineIn(settleMilliseconds);
		while (std::chrono::steady_clock::now() < settleEnd)
		{
			client.update();
			Orkige::DebugMessage discarded;
			while (client.receive(discarded))
			{
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		Orkige::DebugMessage state;
		if (!waitForMessage(client, Protocol::MSG_OBJECT_STATE, state,
			timeoutMilliseconds))
		{
			return false;
		}
		return positionY(state, outY);
	}
}

int main(int argc, char** argv)
{
	if (argc != 3)
	{
		std::fprintf(stderr,
			"usage: player_debug_driver <orkige_player> <scene.oscene>\n");
		return 2;
	}
	const std::string playerBinary = argv[1];
	const std::string scenePath = argv[2];

	// pick a free localhost port: bind an ephemeral listener through the
	// same core code the player uses, read the port back, close it again.
	// (tiny race until the player re-binds it - acceptable for a dev tool)
	unsigned short port = 0;
	{
		Orkige::DebugServer portProbe;
		if (!portProbe.start(0))
		{
			return fail("could not probe for a free port");
		}
		port = portProbe.getPort();
	}
	log("using port " + std::to_string(port));

	// spawn the player (posix_spawn keeps this driver dependency-free; the
	// editor uses SDL_CreateProcess for the same job)
	const std::string portString = std::to_string(port);
	const char* args[] = { playerBinary.c_str(), scenePath.c_str(),
		"--debug-port", portString.c_str(), nullptr };
	if (::posix_spawn(&playerPid, playerBinary.c_str(), nullptr, nullptr,
		const_cast<char* const*>(args), environ) != 0)
	{
		return fail("posix_spawn '" + playerBinary + "' failed");
	}
	log("spawned player pid " + std::to_string(playerPid));

	// connect with retries - the player needs a few seconds to boot the
	// engine before its DebugServer starts listening
	Orkige::DebugClient client;
	{
		const std::chrono::steady_clock::time_point deadline = deadlineIn(30000);
		while (!client.isConnected())
		{
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return fail("could not connect to the player within 30s");
			}
			int status = 0;
			if (::waitpid(playerPid, &status, WNOHANG) == playerPid)
			{
				playerPid = -1;
				return fail("player exited before the debug link came up");
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
	}
	log("connected");

	// 1. hello + hierarchy
	Orkige::DebugMessage message;
	if (!waitForMessage(client, Protocol::MSG_HELLO, message, 30000))
	{
		return fail("no hello within 10s");
	}
	if (message.version != Protocol::VERSION)
	{
		return fail("hello has wrong protocol version");
	}
	if (message.get(Protocol::FIELD_SCENE).find("falling_cube.oscene") ==
		std::string::npos)
	{
		return fail("hello scene path '" +
			message.get(Protocol::FIELD_SCENE) + "' unexpected");
	}
	if (!waitForMessage(client, Protocol::MSG_HIERARCHY, message, 30000))
	{
		return fail("no hierarchy within 10s");
	}
	{
		Orkige::StringVector const & ids = message.getList(Protocol::LIST_IDS);
		if (ids.size() != 1 || ids[0] != "FallCube")
		{
			return fail("hierarchy does not contain exactly FallCube");
		}
	}
	log("hello + hierarchy ok");

	// 1b. the WHOLE-SCENE transform stream (MSG_SCENE_TRANSFORMS) flows without
	// a select and carries the falling cube: two samples must show y dropping.
	// This is the editor's motion-mirror source (delta-only, ~15Hz).
	{
		Orkige::DebugMessage transforms;
		if (!waitForMessage(client, Protocol::MSG_SCENE_TRANSFORMS, transforms,
			30000))
		{
			return fail("no scene_transforms stream within 30s");
		}
		double firstY = 0.0;
		if (!sceneTransformY(transforms, "FallCube", firstY))
		{
			return fail("scene_transforms did not carry FallCube");
		}
		// wait until a later scene_transforms shows the cube has fallen further
		const std::chrono::steady_clock::time_point deadline = deadlineIn(30000);
		double laterY = firstY;
		for (;;)
		{
			Orkige::DebugMessage sample;
			if (waitForMessage(client, Protocol::MSG_SCENE_TRANSFORMS, sample,
				5000))
			{
				double y = 0.0;
				if (sceneTransformY(sample, "FallCube", y))
				{
					laterY = y;
				}
			}
			if (laterY < firstY - 0.1)
			{
				break;	// the mirror stream reports the fall
			}
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return fail("scene_transforms never showed FallCube falling "
					"(y stuck near " + std::to_string(firstY) + ")");
			}
		}
		log("scene_transforms streams the fall (" + std::to_string(firstY) +
			" -> " + std::to_string(laterY) + ")");
	}

	// 2. select the cube and receive object_state carrying all components
	Orkige::DebugMessage select(Protocol::MSG_SELECT);
	select.set(Protocol::FIELD_ID, "FallCube");
	client.send(select);
	if (!waitForMessage(client, Protocol::MSG_OBJECT_STATE, message, 5000))
	{
		return fail("no object_state after select");
	}
	if (message.get(Protocol::FIELD_ID) != "FallCube" ||
		!message.has("TransformComponent.position") ||
		!message.has("RigidBodyComponent.linear_velocity"))
	{
		return fail("object_state incomplete: " + message.encode());
	}

	// 3. the cube must actually be falling (starts at y=5)
	{
		const std::chrono::steady_clock::time_point deadline = deadlineIn(30000); // generous: first run after a build pays RTSS shader-generation cost
		double y = 5.0;
		for (;;)
		{
			if (!sampleFreshY(client, y, 0))
			{
				return fail("no object_state while waiting for the fall");
			}
			if (y < 4.9)
			{
				break;
			}
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return fail("cube never started falling (y stuck at " +
					std::to_string(y) + ")");
			}
		}
		log("cube is falling (y=" + std::to_string(y) + ")");
	}

	// 4. pause: two samples 500ms apart must be EXACTLY equal
	client.send(Orkige::DebugMessage(Protocol::MSG_PAUSE));
	double pausedY1 = 0.0;
	double pausedY2 = 0.0;
	if (!sampleFreshY(client, pausedY1, 500))
	{
		return fail("no object_state after pause");
	}
	if (!sampleFreshY(client, pausedY2, 500))
	{
		return fail("no second object_state while paused");
	}
	if (pausedY1 != pausedY2)
	{
		return fail("cube moved while paused: " + std::to_string(pausedY1) +
			" -> " + std::to_string(pausedY2));
	}
	log("pause holds (y=" + std::to_string(pausedY1) + ")");

	// 5. resume: y must drop again
	client.send(Orkige::DebugMessage(Protocol::MSG_RESUME));
	double resumedY = 0.0;
	if (!sampleFreshY(client, resumedY, 500))
	{
		return fail("no object_state after resume");
	}
	if (!(resumedY < pausedY2 - 0.01))
	{
		return fail("cube did not fall after resume: " +
			std::to_string(pausedY2) + " -> " + std::to_string(resumedY));
	}
	log("resume falls (y=" + std::to_string(resumedY) + ")");

	// 6. step while paused: exactly one tick - y drops once, then holds
	client.send(Orkige::DebugMessage(Protocol::MSG_PAUSE));
	double stepBaseY = 0.0;
	if (!sampleFreshY(client, stepBaseY, 500))
	{
		return fail("no object_state after second pause");
	}
	client.send(Orkige::DebugMessage(Protocol::MSG_STEP));
	double steppedY = 0.0;
	if (!sampleFreshY(client, steppedY, 300))
	{
		return fail("no object_state after step");
	}
	if (!(steppedY < stepBaseY))
	{
		return fail("step did not advance the fall: " +
			std::to_string(stepBaseY) + " -> " + std::to_string(steppedY));
	}
	double afterStepY = 0.0;
	if (!sampleFreshY(client, afterStepY, 300))
	{
		return fail("no object_state after the step settled");
	}
	if (steppedY != afterStepY)
	{
		return fail("cube kept moving after a single step: " +
			std::to_string(steppedY) + " -> " + std::to_string(afterStepY));
	}
	log("step advances exactly one tick (" + std::to_string(stepBaseY) +
		" -> " + std::to_string(steppedY) + ")");

	// 7. set_property: teleport the paused cube to y=10
	Orkige::DebugMessage setPosition(Protocol::MSG_SET_PROPERTY);
	setPosition.set(Protocol::FIELD_ID, "FallCube");
	setPosition.set(Protocol::FIELD_COMPONENT, "TransformComponent");
	setPosition.set(Protocol::FIELD_PROPERTY, "position");
	setPosition.set(Protocol::FIELD_VALUE, "0 10 0");
	client.send(setPosition);
	double teleportedY = 0.0;
	if (!sampleFreshY(client, teleportedY, 300))
	{
		return fail("no object_state after set_property");
	}
	if (std::fabs(teleportedY - 10.0) > 1e-3)
	{
		return fail("set_property position did not apply: y=" +
			std::to_string(teleportedY));
	}
	log("set_property position applied (y=10)");

	// 8. unknown property: an error message answers, the link stays alive
	Orkige::DebugMessage setBogus(Protocol::MSG_SET_PROPERTY);
	setBogus.set(Protocol::FIELD_ID, "FallCube");
	setBogus.set(Protocol::FIELD_COMPONENT, "TransformComponent");
	setBogus.set(Protocol::FIELD_PROPERTY, "colour");
	setBogus.set(Protocol::FIELD_VALUE, "1 2 3");
	client.send(setBogus);
	if (!waitForMessage(client, Protocol::MSG_ERROR, message, 5000))
	{
		return fail("no error reply for an unknown property");
	}
	if (!client.isConnected())
	{
		return fail("link died on an unknown property");
	}
	log("unknown property answered with: " +
		message.get(Protocol::FIELD_MESSAGE));

	// 9. quit: bye comes back and the player exits cleanly
	client.send(Orkige::DebugMessage(Protocol::MSG_QUIT));
	if (!waitForMessage(client, Protocol::MSG_BYE, message, 5000))
	{
		log("note: no bye before the connection dropped (acceptable)");
	}
	{
		const std::chrono::steady_clock::time_point deadline = deadlineIn(10000);
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
			client.update();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		{
			return fail("player exit status not clean (status " +
				std::to_string(status) + ")");
		}
	}
	log("PASSED - full pause/resume/step/set_property/quit contract holds");
	return 0;
}
