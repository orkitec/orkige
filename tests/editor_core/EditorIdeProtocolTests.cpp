/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorIdeProtocolTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// EditorIdeProtocolTests - the pure Claude-IDE protocol layer: the discovery
// lock model, the file-uri and languageId mappings, the MCP result/notification
// builders and the selection diff. No sockets, no editor state.

#include "EditorIdeProtocol.h"

#include <core_debugnet/Json.h>

#include <catch2/catch_test_macros.hpp>

using namespace OrkigeEditor;
using Orkige::JsonValue;

namespace
{
	//! parse the inner JSON body a tool result carries in content[0].text
	JsonValue toolBody(JsonValue const& result)
	{
		JsonValue const& content = result.get("content");
		REQUIRE(content.size() >= 1);
		JsonValue body;
		REQUIRE(JsonValue::parse(content.at(0).get("text").asString(), body));
		return body;
	}
}

TEST_CASE("ide lock round-trips through serialize/parse", "[ide]")
{
	IdeLockInfo info;
	info.pid = 4242;
	info.workspaceFolders = { "/home/dev/project", "/home/dev/other" };
	info.ideName = "Orkige";
	info.transport = "ws";
	info.authToken = "deadbeefcafef00d";

	const std::string text = serializeIdeLock(info);
	// the fields claude reads must be present verbatim
	REQUIRE(text.find("\"authToken\":\"deadbeefcafef00d\"") != std::string::npos);
	REQUIRE(text.find("\"transport\":\"ws\"") != std::string::npos);

	IdeLockInfo parsed;
	REQUIRE(parseIdeLock(text, parsed));
	REQUIRE(parsed.pid == 4242);
	REQUIRE(parsed.workspaceFolders.size() == 2);
	REQUIRE(parsed.workspaceFolders[0] == "/home/dev/project");
	REQUIRE(parsed.ideName == "Orkige");
	REQUIRE(parsed.authToken == "deadbeefcafef00d");
}

TEST_CASE("ide lock parse rejects malformed input", "[ide]")
{
	IdeLockInfo out;
	REQUIRE_FALSE(parseIdeLock("not json", out));
	REQUIRE_FALSE(parseIdeLock("[1,2,3]", out));	// not an object
	REQUIRE_FALSE(parseIdeLock("", out));
}

TEST_CASE("a real VS Code lock body parses (interop shape)", "[ide]")
{
	// the exact shape a real IDE writes (UUID token, extra fields we ignore)
	const std::string body =
		"{\"pid\":25765,\"workspaceFolders\":[\"/Users/dev/proj\"],"
		"\"ideName\":\"Visual Studio Code\",\"transport\":\"ws\","
		"\"runningInWindows\":false,"
		"\"authToken\":\"8d24881d-e74e-41c4-a988-c72a45a2a05c\"}";
	IdeLockInfo parsed;
	REQUIRE(parseIdeLock(body, parsed));
	REQUIRE(parsed.pid == 25765);
	REQUIRE(parsed.authToken == "8d24881d-e74e-41c4-a988-c72a45a2a05c");
	REQUIRE(parsed.workspaceFolders.size() == 1);
}

TEST_CASE("stale-lock overwrite follows the owner pid liveness", "[ide]")
{
	REQUIRE(ideLockMayOverwrite(false));		// dead owner: reclaim it
	REQUIRE_FALSE(ideLockMayOverwrite(true));	// live owner: leave it
}

TEST_CASE("languageId mapping covers the house kinds", "[ide]")
{
	REQUIRE(ideLanguageForPath("scripts/player.lua") == "lua");
	REQUIRE(ideLanguageForPath("scene.oscene") == "xml");
	REQUIRE(ideLanguageForPath("data.json") == "json");
	REQUIRE(ideLanguageForPath("README.md") == "markdown");
	REQUIRE(ideLanguageForPath("main.cpp") == "cpp");
	REQUIRE(ideLanguageForPath("hud.oui") == "plaintext");
	REQUIRE(ideLanguageForPath("mystery.zzz") == "plaintext");
}

TEST_CASE("file uri round-trips and percent-encodes", "[ide]")
{
	REQUIRE(idePathToFileUri("/a/b/c.lua") == "file:///a/b/c.lua");
	// a space must be encoded, then decode back exactly
	const std::string uri = idePathToFileUri("/a b/c.lua");
	REQUIRE(uri == "file:///a%20b/c.lua");
	REQUIRE(ideFileUriToPath(uri) == "/a b/c.lua");
	REQUIRE(ideFileUriToPath("not-a-file-uri").empty());
}

TEST_CASE("initialize result advertises tools + serverInfo", "[ide]")
{
	JsonValue result = ideInitializeResult("2025-03-26", "orkige-editor", "1.2");
	REQUIRE(result.get("protocolVersion").asString() == "2025-03-26");
	REQUIRE(result.get("capabilities").get("tools").isObject());
	REQUIRE(result.get("serverInfo").get("name").asString() == "orkige-editor");
	REQUIRE(result.get("serverInfo").get("version").asString() == "1.2");
}

TEST_CASE("tool list advertises the IDE verbs", "[ide]")
{
	JsonValue tools = ideToolList();
	REQUIRE(tools.isArray());
	bool hasOpenFile = false, hasDiag = false, hasWorkspace = false;
	for (size_t i = 0; i < tools.size(); ++i)
	{
		const Orkige::String name = tools.at(i).get("name").asString();
		if (name == "openFile") hasOpenFile = true;
		if (name == "getDiagnostics") hasDiag = true;
		if (name == "getWorkspaceFolders") hasWorkspace = true;
		// every tool carries an object inputSchema
		REQUIRE(tools.at(i).get("inputSchema").get("type").asString() ==
			"object");
	}
	REQUIRE(hasOpenFile);
	REQUIRE(hasDiag);
	REQUIRE(hasWorkspace);
}

TEST_CASE("tool error carries isError", "[ide]")
{
	JsonValue error = ideToolError("nope");
	REQUIRE(error.get("isError").asBool());
	REQUIRE(error.get("content").at(0).get("text").asString() == "nope");
}

TEST_CASE("workspace folders result names the root", "[ide]")
{
	JsonValue result = ideWorkspaceFoldersResult({ "/home/dev/proj" });
	JsonValue body = toolBody(result);
	REQUIRE(body.get("success").asBool());
	REQUIRE(body.get("rootPath").asString() == "/home/dev/proj");
	REQUIRE(body.get("folders").at(0).get("uri").asString() ==
		"file:///home/dev/proj");
	REQUIRE(body.get("folders").at(0).get("path").asString() ==
		"/home/dev/proj");
}

TEST_CASE("open editors result carries active/dirty/language", "[ide]")
{
	IdeEditor a;
	a.absolutePath = "/p/player.lua";
	a.languageId = "lua";
	a.active = true;
	a.dirty = true;
	IdeEditor b;
	b.absolutePath = "/p/scene.oscene";
	b.languageId = "xml";
	JsonValue body = toolBody(ideOpenEditorsResult({ a, b }));
	JsonValue const& tabs = body.get("tabs");
	REQUIRE(tabs.size() == 2);
	REQUIRE(tabs.at(0).get("label").asString() == "player.lua");
	REQUIRE(tabs.at(0).get("isActive").asBool());
	REQUIRE(tabs.at(0).get("isDirty").asBool());
	REQUIRE(tabs.at(0).get("languageId").asString() == "lua");
	REQUIRE(tabs.at(1).get("isActive").asBool() == false);
}

TEST_CASE("selection result: active carries the range and text", "[ide]")
{
	IdeSelection sel;
	sel.active = true;
	sel.absolutePath = "/p/player.lua";
	sel.text = "local x";
	sel.startLine = 3;
	sel.startChar = 2;
	sel.endLine = 3;
	sel.endChar = 9;
	JsonValue body = toolBody(ideSelectionResult(sel));
	REQUIRE(body.get("success").asBool());
	REQUIRE(body.get("text").asString() == "local x");
	REQUIRE(body.get("filePath").asString() == "/p/player.lua");
	REQUIRE(body.get("selection").get("start").get("line").asInt() == 3);
	REQUIRE(body.get("selection").get("isEmpty").asBool() == false);
}

TEST_CASE("selection result: inactive reports no selection", "[ide]")
{
	JsonValue body = toolBody(ideSelectionResult(IdeSelection()));
	REQUIRE(body.get("success").asBool() == false);
}

TEST_CASE("diagnostics group by file with 0-based lines", "[ide]")
{
	IdeDiagnostic a;
	a.absolutePath = "/p/a.lua";
	a.line = 5;			// 1-based -> LSP line 4
	a.message = "boom";
	IdeDiagnostic b;
	b.absolutePath = "/p/a.lua";
	b.line = 1;
	b.message = "second";
	IdeDiagnostic c;
	c.absolutePath = "/p/b.oscene";
	c.line = 0;			// unknown line -> 0
	c.message = "bad xml";

	SECTION("all files")
	{
		JsonValue root = toolBody(ideDiagnosticsResult({ a, b, c }, ""));
		REQUIRE(root.size() == 2);	// two distinct files
		JsonValue const& first = root.at(0);
		REQUIRE(first.get("uri").asString() == "file:///p/a.lua");
		REQUIRE(first.get("diagnostics").size() == 2);
		REQUIRE(first.get("diagnostics").at(0).get("range").get("start")
			.get("line").asInt() == 4);
	}
	SECTION("filtered to one file")
	{
		JsonValue root =
			toolBody(ideDiagnosticsResult({ a, b, c }, "/p/b.oscene"));
		REQUIRE(root.size() == 1);
		REQUIRE(root.at(0).get("uri").asString() == "file:///p/b.oscene");
	}
}

TEST_CASE("selection_changed diff fires only on a real move", "[ide]")
{
	IdeSelection a;
	a.active = true;
	a.absolutePath = "/p/a.lua";
	a.startLine = 1;
	IdeSelection b = a;
	REQUIRE_FALSE(ideSelectionChanged(a, b));	// identical
	b.startLine = 2;
	REQUIRE(ideSelectionChanged(a, b));			// moved
	IdeSelection inactive1, inactive2;
	REQUIRE_FALSE(ideSelectionChanged(inactive1, inactive2));	// both idle
	REQUIRE(ideSelectionChanged(a, inactive1));	// active -> idle
}

TEST_CASE("notification envelope is a JSON-RPC notification", "[ide]")
{
	JsonValue note = ideNotification("selection_changed",
		ideSelectionParams(IdeSelection()));
	REQUIRE(note.get("jsonrpc").asString() == "2.0");
	REQUIRE(note.get("method").asString() == "selection_changed");
	REQUIRE_FALSE(note.has("id"));	// a notification carries no id
}
