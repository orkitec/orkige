/**************************************************************
	created:	2026/07/07 at 23:30
	filename: 	DebugProtocolTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <core_debugnet/DebugProtocol.h>
#include <core_debugnet/DebugServer.h>
#include <core_debugnet/DebugClient.h>

#ifndef _WIN32
#	include <arpa/inet.h>
#	include <netinet/in.h>
#	include <sys/socket.h>
#	include <unistd.h>
#endif

#include <chrono>
#include <cstring>
#include <thread>

using Orkige::DebugMessage;
using Orkige::DebugClient;
using Orkige::DebugServer;
namespace Protocol = Orkige::DebugProtocol;

namespace
{
	//! encode -> decode and require the result to match the input exactly
	DebugMessage roundTrip(DebugMessage const & in)
	{
		const Orkige::String line = in.encode();
		DebugMessage out;
		REQUIRE(DebugMessage::decode(line, out));
		CHECK(out.version == in.version);
		CHECK(out.type == in.type);
		CHECK(out.fields == in.fields);
		CHECK(out.lists == in.lists);
		return out;
	}
	//---------------------------------------------------------
	//! pump server and client until predicate() or the deadline; the loop is
	//! single-threaded and deterministic - only the wall-clock deadline (very
	//! generous, loopback traffic lands in a few pumps) is time-based
	template <typename Predicate>
	bool pumpUntil(DebugServer & server, DebugClient & client,
		Predicate predicate, int timeoutMilliseconds = 5000)
	{
		const std::chrono::steady_clock::time_point deadline =
			std::chrono::steady_clock::now() +
			std::chrono::milliseconds(timeoutMilliseconds);
		for (;;)
		{
			server.update();
			client.update();
			if (predicate())
			{
				return true;
			}
			if (std::chrono::steady_clock::now() >= deadline)
			{
				return false;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}

TEST_CASE("DebugMessage round-trips every protocol message type", "[debugnet]")
{
	SECTION("plain commands (pause/resume/step/quit/request_hierarchy/bye)")
	{
		for (Orkige::String const & type : { Protocol::MSG_PAUSE,
			Protocol::MSG_RESUME, Protocol::MSG_STEP, Protocol::MSG_QUIT,
			Protocol::MSG_REQUEST_HIERARCHY, Protocol::MSG_BYE })
		{
			roundTrip(DebugMessage(type));
		}
	}
	SECTION("hello")
	{
		DebugMessage hello(Protocol::MSG_HELLO);
		hello.set(Protocol::FIELD_SCENE, "/tmp/some scene/play.oscene");
		roundTrip(hello);
	}
	SECTION("select")
	{
		DebugMessage select(Protocol::MSG_SELECT);
		select.set(Protocol::FIELD_ID, "Cube1");
		roundTrip(select);
	}
	SECTION("set_property")
	{
		DebugMessage set(Protocol::MSG_SET_PROPERTY);
		set.set(Protocol::FIELD_ID, "Cube1");
		set.set(Protocol::FIELD_COMPONENT, "TransformComponent");
		set.set(Protocol::FIELD_PROPERTY, "position");
		set.set(Protocol::FIELD_VALUE, "1.5 -2.25 3e-05");
		roundTrip(set);
	}
	SECTION("hierarchy (id list)")
	{
		DebugMessage hierarchy(Protocol::MSG_HIERARCHY);
		hierarchy.setList(Protocol::LIST_IDS,
			{ "Cube1", "Cube2", "TestMesh1" });
		roundTrip(hierarchy);
	}
	SECTION("hierarchy with parent and active lists (additive v1 extension)")
	{
		DebugMessage hierarchy(Protocol::MSG_HIERARCHY);
		hierarchy.setList(Protocol::LIST_IDS,
			{ "Group", "Tile1", "Tile2" });
		// "" = root; parents/actives are parallel to ids
		hierarchy.setList(Protocol::LIST_PARENTS, { "", "Group", "Group" });
		hierarchy.setList(Protocol::LIST_ACTIVE, { "1", "1", "0" });
		roundTrip(hierarchy);
	}
	SECTION("set_active")
	{
		DebugMessage setActive(Protocol::MSG_SET_ACTIVE);
		setActive.set(Protocol::FIELD_ID, "Group");
		setActive.set(Protocol::FIELD_VALUE, "0");
		roundTrip(setActive);
	}
	SECTION("set_cvar")
	{
		DebugMessage setCvar(Protocol::MSG_SET_CVAR);
		setCvar.set(Protocol::FIELD_CVAR_NAME, "roller_gravity");
		setCvar.set(Protocol::FIELD_VALUE, "30.5");
		DebugMessage out = roundTrip(setCvar);
		CHECK(out.get(Protocol::FIELD_CVAR_NAME) == "roller_gravity");
		CHECK(out.get(Protocol::FIELD_VALUE) == "30.5");
	}
	SECTION("object_state")
	{
		DebugMessage state(Protocol::MSG_OBJECT_STATE);
		state.set(Protocol::FIELD_ID, "Cube1");
		state.setList(Protocol::LIST_COMPONENTS,
			{ "ModelComponent", "TransformComponent" });
		state.set("TransformComponent.position", "0 5 0");
		state.set("TransformComponent.orientation", "1 0 0 0");
		state.set("TransformComponent.scale", "1 1 1");
		state.set("ModelComponent.mesh", "EditorCube.mesh");
		roundTrip(state);
	}
	SECTION("log and error")
	{
		DebugMessage log(Protocol::MSG_LOG);
		log.set(Protocol::FIELD_MESSAGE, "physics world up");
		roundTrip(log);
		DebugMessage error(Protocol::MSG_ERROR);
		error.set(Protocol::FIELD_MESSAGE,
			"unknown property 'colour' on 'TransformComponent'");
		roundTrip(error);
	}
	SECTION("script_error (pushed per failed ScriptComponent)")
	{
		DebugMessage scriptError(Protocol::MSG_SCRIPT_ERROR);
		scriptError.set(Protocol::FIELD_ID, "Player");
		scriptError.set(Protocol::FIELD_MESSAGE,
			"init: scripts/player.lua:12: attempt to index a nil value "
			"(global 'engin')");
		const DebugMessage out = roundTrip(scriptError);
		CHECK(out.type == "script_error");
		CHECK(out.get(Protocol::FIELD_ID) == "Player");
	}
	SECTION("script debugger family (breakpoints/break/step/locals)")
	{
		// the full-list breakpoint replace
		DebugMessage breakpoints(Protocol::MSG_DEBUG_BREAKPOINTS);
		breakpoints.setList(Protocol::LIST_BREAKPOINTS,
			{ "scripts/player.lua:12", "scripts/enemy.lua:3" });
		DebugMessage out = roundTrip(breakpoints);
		REQUIRE(out.getList(Protocol::LIST_BREAKPOINTS).size() == 2);
		CHECK(out.getList(Protocol::LIST_BREAKPOINTS)[0] ==
			"scripts/player.lua:12");
		// the break-hit notification with its parallel stack lists
		DebugMessage breakHit(Protocol::MSG_DEBUG_BREAK);
		breakHit.set(Protocol::FIELD_PATH, "scripts/player.lua");
		breakHit.set(Protocol::FIELD_LINE, "12");
		breakHit.setList(Protocol::LIST_STACK_SOURCES,
			{ "scripts/player.lua", "[host]" });
		breakHit.setList(Protocol::LIST_STACK_LINES, { "12", "-1" });
		breakHit.setList(Protocol::LIST_STACK_FUNCTIONS, { "update", "" });
		out = roundTrip(breakHit);
		CHECK(out.get(Protocol::FIELD_PATH) == "scripts/player.lua");
		CHECK(out.get(Protocol::FIELD_LINE) == "12");
		REQUIRE(out.getList(Protocol::LIST_STACK_SOURCES).size() == 2);
		// the plain release commands + the break-on-next-statement arm
		for (Orkige::String const & type : { Protocol::MSG_DEBUG_RESUME,
			Protocol::MSG_DEBUG_STEP_IN, Protocol::MSG_DEBUG_STEP_OVER,
			Protocol::MSG_DEBUG_STEP_OUT, Protocol::MSG_DEBUG_RESUMED,
			Protocol::MSG_DEBUG_BREAK_NEXT })
		{
			roundTrip(DebugMessage(type));
		}
		// the locals request (frame + expand path) and its reply lists
		DebugMessage locals(Protocol::MSG_DEBUG_LOCALS);
		locals.set(Protocol::FIELD_FRAME, "1");
		locals.setList(Protocol::LIST_EXPAND_PATH, { "self", "[3]" });
		locals.setList(Protocol::LIST_VAR_NAMES, { "id", "speed" });
		locals.setList(Protocol::LIST_VAR_SCOPES, { "field", "field" });
		locals.setList(Protocol::LIST_VAR_TYPES, { "string", "number" });
		locals.setList(Protocol::LIST_VAR_VALUES, { "\"Player\"", "4.5" });
		locals.setList(Protocol::LIST_VAR_EXPANDABLE, { "0", "0" });
		out = roundTrip(locals);
		CHECK(out.get(Protocol::FIELD_FRAME) == "1");
		REQUIRE(out.getList(Protocol::LIST_EXPAND_PATH).size() == 2);
		CHECK(out.getList(Protocol::LIST_VAR_VALUES)[0] == "\"Player\"");
	}
	SECTION("scene_transforms (whole-scene motion mirror, parallel lists)")
	{
		DebugMessage transforms(Protocol::MSG_SCENE_TRANSFORMS);
		transforms.setList(Protocol::LIST_IDS, { "FallCube", "Group/Tile1" });
		// one flat "px py pz qw qx qy qz sx sy sz" string per id
		transforms.setList(Protocol::LIST_TRANSFORMS,
			{ "0 4.87 0 1 0 0 0 1 1 1",
			  "-2.5 0 1.25 0.7071 0 0.7071 0 2 2 2" });
		const DebugMessage out = roundTrip(transforms);
		CHECK(out.type == "scene_transforms");
		REQUIRE(out.getList(Protocol::LIST_IDS).size() == 2);
		CHECK(out.getList(Protocol::LIST_IDS)[0] == "FallCube");
		CHECK(out.getList(Protocol::LIST_TRANSFORMS)[0] ==
			"0 4.87 0 1 0 0 0 1 1 1");
	}
	SECTION("scene_loaded (mid-play scene switch notice)")
	{
		DebugMessage loaded(Protocol::MSG_SCENE_LOADED);
		loaded.set(Protocol::FIELD_SCENE, "scenes/second.oscene");
		const DebugMessage out = roundTrip(loaded);
		CHECK(out.type == "scene_loaded");
		CHECK(out.get(Protocol::FIELD_SCENE) == "scenes/second.oscene");
	}
	SECTION("query_spawns / scene_spawns (runtime-spawn descriptors)")
	{
		DebugMessage query(Protocol::MSG_QUERY_SPAWNS);
		query.setList(Protocol::LIST_IDS, { "RuntimeProbe" });
		const DebugMessage queryOut = roundTrip(query);
		CHECK(queryOut.type == "query_spawns");
		REQUIRE(queryOut.getList(Protocol::LIST_IDS).size() == 1);

		DebugMessage spawns(Protocol::MSG_SCENE_SPAWNS);
		spawns.setList(Protocol::LIST_IDS, { "RuntimeProbe" });
		spawns.setList(Protocol::LIST_PARENTS, { "" });
		spawns.setList(Protocol::LIST_COMPONENTS,
			{ "TransformComponent ModelComponent" });
		// the flat per-property quintuple (object by INDEX into LIST_IDS; the
		// value list is its own list, so no value ever needs escaping)
		spawns.setList(Protocol::LIST_SPAWN_OBJECTS, { "0", "0" });
		spawns.setList(Protocol::LIST_PROP_KEYS,
			{ "TransformComponent.position", "ModelComponent.mesh" });
		spawns.setList(Protocol::LIST_SPAWN_KINDS, { "5", "8" });
		spawns.setList(Protocol::LIST_SPAWN_VALUES,
			{ "0 1 0", "EditorCube.mesh" });
		spawns.setList(Protocol::LIST_SPAWN_REFS, { "", "asset-id-9" });
		const DebugMessage out = roundTrip(spawns);
		CHECK(out.type == "scene_spawns");
		REQUIRE(out.getList(Protocol::LIST_IDS).size() == 1);
		CHECK(out.getList(Protocol::LIST_COMPONENTS)[0] ==
			"TransformComponent ModelComponent");
		REQUIRE(out.getList(Protocol::LIST_SPAWN_VALUES).size() == 2);
		CHECK(out.getList(Protocol::LIST_SPAWN_VALUES)[1] ==
			"EditorCube.mesh");
		CHECK(out.getList(Protocol::LIST_SPAWN_REFS)[1] == "asset-id-9");
	}
}

TEST_CASE("DebugMessage carries request-correlation and auth fields",
	"[debugnet]")
{
	// the MCP control port echoes a request's "req" id in its ok/err reply and
	// presents an auth token in a hello - both are plain additive fields, so
	// they round-trip through the existing codec unchanged
	DebugMessage hello(Protocol::MSG_HELLO);
	hello.set(Protocol::FIELD_TOKEN, "5f2c9ab1deadbeef5f2c9ab1deadbeef");
	hello.set(Protocol::FIELD_REQ, "r42");
	const DebugMessage out = roundTrip(hello);
	CHECK(out.get(Protocol::FIELD_TOKEN) ==
		"5f2c9ab1deadbeef5f2c9ab1deadbeef");
	CHECK(out.get(Protocol::FIELD_REQ) == "r42");

	DebugMessage ok("ok");
	ok.set(Protocol::FIELD_REQ, "r42");
	ok.set("scene_dirty", "0");
	const DebugMessage okOut = roundTrip(ok);
	CHECK(okOut.type == "ok");
	CHECK(okOut.get(Protocol::FIELD_REQ) == "r42");
}

TEST_CASE("DebugMessage::decode is safe on a temporary source string",
	"[debugnet]")
{
	// the JsonReader keeps a reference to the source text; decode()'s String
	// const& parameter keeps a passed temporary alive for the whole call, so
	// decoding the result of an expression must be safe (the fix deletes the
	// reader's rvalue ctor so an INTERNAL temporary can never be bound)
	DebugMessage source(Protocol::MSG_SELECT);
	source.set(Protocol::FIELD_ID, "TempCube");
	DebugMessage out;
	REQUIRE(DebugMessage::decode(source.encode(), out)); // temporary argument
	CHECK(out.type == Protocol::MSG_SELECT);
	CHECK(out.get(Protocol::FIELD_ID) == "TempCube");
}

TEST_CASE("DebugMessage escapes and restores edge-case values", "[debugnet]")
{
	DebugMessage in("select");
	in.set("quotes", "he said \"hi\" \\ and \\\\ again");
	in.set("control", Orkige::String("line1\nline2\ttabbed\rreturn") +
		Orkige::String(1, '\x01') + Orkige::String(1, '\x1f'));
	in.set("unicode", "\xc3\xbc\x62\x65r cube \xe2\x82\xac"); // über cube €
	in.set("empty", "");
	in.set("json-lookalike", "{\"v\":1,\"type\":\"fake\"}");
	in.setList("weird ids", { "a,b", "", "with \"quote\"", "newline\nid" });
	const DebugMessage out = roundTrip(in);
	CHECK(out.get("quotes") == in.get("quotes"));
	CHECK(out.get("empty") == "");
	CHECK(out.has("empty"));
	CHECK_FALSE(out.has("missing"));
	CHECK(out.get("missing") == "");
	// the encoded line must stay a single line (framing!) - every control
	// character must have been escaped
	const Orkige::String encoded = in.encode();
	CHECK(encoded.find('\n') == Orkige::String::npos);
	CHECK(encoded.find('\r') == Orkige::String::npos);
}

TEST_CASE("DebugMessage float helpers round-trip precisely", "[debugnet]")
{
	DebugMessage in("object_state");
	in.setFloat("y", 4.9273195f);
	in.setFloat("tiny", 3e-05f);
	in.setFloat("negative", -123456.78f);
	DebugMessage out;
	REQUIRE(DebugMessage::decode(in.encode(), out));
	CHECK(out.getFloat("y") == 4.9273195f);
	CHECK(out.getFloat("tiny") == 3e-05f);
	CHECK(out.getFloat("negative") == -123456.78f);
	CHECK(out.getFloat("missing", 42.0f) == 42.0f);
}

TEST_CASE("DebugMessage decodes foreign but valid flat JSON", "[debugnet]")
{
	// numbers, booleans and null arrive as their literal text; whitespace
	// between tokens is tolerated; "v" and "type" map to the members
	DebugMessage out;
	REQUIRE(DebugMessage::decode(
		"{ \"v\" : 1 , \"type\" : \"select\" , \"n\" : -42.5e1 , "
		"\"b\" : true , \"z\" : null , \"ids\" : [ \"a\" , 7 ] }", out));
	CHECK(out.version == 1);
	CHECK(out.type == "select");
	CHECK(out.get("n") == "-42.5e1");
	CHECK(out.getFloat("n") == -425.0f);
	CHECK(out.get("b") == "true");
	CHECK(out.get("z") == "null");
	REQUIRE(out.getList("ids").size() == 2);
	CHECK(out.getList("ids")[0] == "a");
	CHECK(out.getList("ids")[1] == "7");
	// \uXXXX escapes decode to UTF-8, including surrogate pairs
	DebugMessage unicode;
	REQUIRE(DebugMessage::decode(
		"{\"v\":1,\"type\":\"log\",\"message\":\"\\u00e9\\u20ac\\ud83d\\ude00\"}",
		unicode));
	CHECK(unicode.get("message") == "\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
}

TEST_CASE("DebugMessage rejects malformed input without crashing", "[debugnet]")
{
	DebugMessage out;
	out.type = "sentinel"; // decode failures must leave out untouched
	const char* malformed[] = {
		"",
		"   ",
		"garbage",
		"[]",
		"{",
		"}",
		"{}", // no type
		"{\"v\":1}", // no type
		"{\"v\":1,\"type\":\"pa", // truncated mid-string
		"{\"v\":1,\"type\":\"pause\"", // missing brace
		"{\"v\":1,\"type\":\"pause\",}", // trailing comma
		"{\"v\":1,\"type\":\"pause\"}x", // trailing garbage
		"{\"v\":1,\"type\":\"pause\"}{\"v\":1,\"type\":\"resume\"}",
		"{\"v\":1,\"type\":{}}", // nested object
		"{\"v\":1,\"type\":\"x\",\"o\":{\"a\":1}}", // nested object value
		"{\"v\":1,\"type\":\"x\",\"a\":[[1]]}", // nested array
		"{\"v\":1,\"type\":\"x\",\"a\":[}", // broken array
		"{\"v\":1,\"type\":\"x\",\"bad\":bareword}", // invalid bare scalar
		"{\"v\":1,\"type\":\"x\",\"bad\":12x4}", // invalid number
		"{\"v\":1,\"type\":\"x\",\"e\":\"\\q\"}", // unknown escape
		"{\"v\":1,\"type\":\"x\",\"e\":\"\\u12\"}", // short unicode escape
		"{\"v\":1,\"type\":\"x\" \"y\":1}", // missing comma
		"{v:1,type:pause}", // unquoted keys
		"\x00\x01\x02\xff\xfe garbage bytes",
	};
	for (const char* line : malformed)
	{
		INFO("input: " << line);
		CHECK_FALSE(DebugMessage::decode(line, out));
		CHECK(out.type == "sentinel");
	}
	// fuzz-ish: truncate a valid message at every byte - none may decode to
	// a different message or crash
	const Orkige::String valid =
		DebugMessage(Protocol::MSG_PAUSE).encode();
	for (size_t length = 0; length < valid.size(); ++length)
	{
		DebugMessage truncatedOut;
		CHECK_FALSE(DebugMessage::decode(valid.substr(0, length), truncatedOut));
	}
	// a huge (well beyond MAX_LINE_LENGTH) non-JSON line must fail cleanly;
	// the transport cap is exercised in the loopback test below
	const Orkige::String huge(2 * Protocol::MAX_LINE_LENGTH, 'A');
	CHECK_FALSE(DebugMessage::decode(huge, out));
}

TEST_CASE("DebugServer and DebugClient exchange messages over a real localhost socket", "[debugnet]")
{
	DebugServer server;
	REQUIRE(server.start(0)); // ephemeral port
	REQUIRE(server.isListening());
	const unsigned short port = server.getPort();
	REQUIRE(port != 0);

	DebugClient client;
	REQUIRE(client.connect("127.0.0.1", port));
	REQUIRE(pumpUntil(server, client, [&] {
		return server.hasClient() && client.isConnected();
	}));
	CHECK(server.consumeClientConnected());
	CHECK_FALSE(server.consumeClientConnected()); // edge, not level

	// runtime -> editor: hello
	DebugMessage hello(Protocol::MSG_HELLO);
	hello.set(Protocol::FIELD_SCENE, "loopback.oscene");
	REQUIRE(server.send(hello));
	DebugMessage received;
	REQUIRE(pumpUntil(server, client, [&] { return client.receive(received); }));
	CHECK(received.type == Protocol::MSG_HELLO);
	CHECK(received.version == Protocol::VERSION);
	CHECK(received.get(Protocol::FIELD_SCENE) == "loopback.oscene");

	// editor -> runtime: pause; framing: three messages sent back-to-back
	// must arrive as three distinct messages in order
	REQUIRE(client.send(DebugMessage(Protocol::MSG_PAUSE)));
	DebugMessage select(Protocol::MSG_SELECT);
	select.set(Protocol::FIELD_ID, "FallCube");
	REQUIRE(client.send(select));
	REQUIRE(client.send(DebugMessage(Protocol::MSG_STEP)));
	std::vector<DebugMessage> serverReceived;
	REQUIRE(pumpUntil(server, client, [&] {
		DebugMessage message;
		while (server.receive(message))
		{
			serverReceived.push_back(message);
		}
		return serverReceived.size() >= 3;
	}));
	REQUIRE(serverReceived.size() == 3);
	CHECK(serverReceived[0].type == Protocol::MSG_PAUSE);
	CHECK(serverReceived[1].type == Protocol::MSG_SELECT);
	CHECK(serverReceived[1].get(Protocol::FIELD_ID) == "FallCube");
	CHECK(serverReceived[2].type == Protocol::MSG_STEP);

	// runtime -> editor: object_state with float payload
	DebugMessage state(Protocol::MSG_OBJECT_STATE);
	state.set(Protocol::FIELD_ID, "FallCube");
	state.setList(Protocol::LIST_COMPONENTS, { "TransformComponent" });
	state.set("TransformComponent.position", "0 4.9273195 0");
	REQUIRE(server.send(state));
	REQUIRE(pumpUntil(server, client, [&] { return client.receive(received); }));
	CHECK(received.type == Protocol::MSG_OBJECT_STATE);
	CHECK(received.get("TransformComponent.position") == "0 4.9273195 0");

	// oversized outbound message: sender refuses, link stays healthy
	DebugMessage oversized(Protocol::MSG_LOG);
	oversized.set(Protocol::FIELD_MESSAGE,
		Orkige::String(Protocol::MAX_LINE_LENGTH, 'x'));
	CHECK_FALSE(server.send(oversized));
	REQUIRE(server.send(hello)); // still works after the refusal
	REQUIRE(pumpUntil(server, client, [&] { return client.receive(received); }));
	CHECK(received.type == Protocol::MSG_HELLO);

	// orderly shutdown: bye then disconnect; server notices the drop
	DebugMessage bye(Protocol::MSG_BYE);
	REQUIRE(client.send(bye));
	REQUIRE(pumpUntil(server, client, [&] {
		DebugMessage message;
		return server.receive(message) && message.type == Protocol::MSG_BYE;
	}));
	client.disconnect();
	REQUIRE(pumpUntil(server, client, [&] {
		return server.consumeClientDisconnected();
	}));
	CHECK_FALSE(server.hasClient());
	CHECK(server.getMalformedLineCount() == 0);
}

#ifndef _WIN32
TEST_CASE("DebugServer survives garbage, truncated and oversized raw input", "[debugnet]")
{
	DebugServer server;
	REQUIRE(server.start(0));
	const unsigned short port = server.getPort();

	// raw blocking client socket - this test plays the malicious/broken peer
	const int raw = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	REQUIRE(raw >= 0);
	struct sockaddr_in address;
	std::memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	REQUIRE(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
	REQUIRE(::connect(raw, reinterpret_cast<struct sockaddr*>(&address),
		sizeof(address)) == 0);

	const std::chrono::steady_clock::time_point deadline =
		std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (!server.hasClient() && std::chrono::steady_clock::now() < deadline)
	{
		server.update();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	REQUIRE(server.hasClient());

	// garbage bytes line, truncated JSON line, an oversized line (well past
	// MAX_LINE_LENGTH), then one valid message - the server must drop the
	// junk, stay connected and deliver the valid message
	Orkige::String payload;
	payload += "\x01\x02\xff not json at all\n";
	payload += "{\"v\":1,\"type\":\"pa\n";
	payload += Orkige::String(Protocol::MAX_LINE_LENGTH + 4096, 'A');
	payload += "\n";
	payload += "{\"v\":1,\"type\":\"pause\"}\n";
	size_t sent = 0;
	while (sent < payload.size())
	{
		// pump the server while sending so its receive buffer drains - a
		// blocking sender against a never-reading peer would deadlock
		const long chunk = ::send(raw, payload.data() + sent,
			std::min<size_t>(payload.size() - sent, 8192), 0);
		REQUIRE(chunk > 0);
		sent += static_cast<size_t>(chunk);
		server.update();
	}
	DebugMessage received;
	bool gotValid = false;
	while (!gotValid && std::chrono::steady_clock::now() < deadline)
	{
		server.update();
		while (server.receive(received))
		{
			// nothing but the valid trailing message may ever decode
			REQUIRE(received.type == Protocol::MSG_PAUSE);
			gotValid = true;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(gotValid);
	CHECK(server.hasClient());
	CHECK(server.getMalformedLineCount() == 2); // garbage + truncated
	CHECK(server.getDroppedLineCount() == 1);   // the oversized line

	// a second connection while one is attached is refused (accepted and
	// closed) and the first connection keeps working
	const int second = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	REQUIRE(second >= 0);
	REQUIRE(::connect(second, reinterpret_cast<struct sockaddr*>(&address),
		sizeof(address)) == 0);
	server.update();
	const char* ping = "{\"v\":1,\"type\":\"resume\"}\n";
	REQUIRE(::send(raw, ping, std::strlen(ping), 0) > 0);
	bool gotResume = false;
	while (!gotResume && std::chrono::steady_clock::now() < deadline)
	{
		server.update();
		while (server.receive(received))
		{
			gotResume = (received.type == Protocol::MSG_RESUME);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	CHECK(gotResume);
	::close(second);
	::close(raw);
	// abrupt peer close surfaces as a disconnect
	REQUIRE([&] {
		const std::chrono::steady_clock::time_point dropDeadline =
			std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < dropDeadline)
		{
			server.update();
			if (server.consumeClientDisconnected())
			{
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		return false;
	}());
	CHECK_FALSE(server.hasClient());
}
#endif // !_WIN32
