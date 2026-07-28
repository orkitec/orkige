/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorMcpConfigTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

// EditorMcpConfigTests - the pure project-scope .mcp.json logic: the server
// entry shape, the merge-or-skip decision (fresh file / stale-ours update /
// foreign name skip / other-server preservation / malformed skip) and the
// removal decision (only-server delete / others-remain rewrite / not-ours
// leave). No file IO.

#include "EditorMcpConfig.h"

#include <core_debugnet/Json.h>

#include <catch2/catch_test_macros.hpp>

using namespace OrkigeEditor;
using Orkige::JsonValue;
using Orkige::String;

namespace
{
	//! parse a serialized .mcp.json body (the test asserts on the parsed tree)
	JsonValue parseJson(std::string const& text)
	{
		JsonValue value;
		REQUIRE(JsonValue::parse(String(text.c_str()), value));
		return value;
	}
	//! the server entry for name inside a serialized .mcp.json body
	JsonValue serverEntry(std::string const& body, std::string const& name)
	{
		return parseJson(body).get("mcpServers").get(String(name.c_str()));
	}
}

TEST_CASE("mcp server entry carries the http/url/headers/marker shape", "[mcp]")
{
	const JsonValue entry = buildOrkigeMcpServerEntry(
		"http://127.0.0.1:5123/mcp", "sekret");
	CHECK(entry.get("type").asString() == String("http"));
	CHECK(entry.get("url").asString() == String("http://127.0.0.1:5123/mcp"));
	CHECK(entry.get("headers").get("Authorization").asString() ==
		String("Bearer sekret"));
	// the marker rides so a reconciler only ever manages the entry it wrote
	CHECK(entry.has(String(MCP_MANAGED_MARKER)));
	CHECK(entry.get(String(MCP_MANAGED_MARKER)).asBool());
}

TEST_CASE("an auth-off endpoint writes no headers block", "[mcp]")
{
	const JsonValue entry =
		buildOrkigeMcpServerEntry("http://127.0.0.1:5123/mcp", "");
	CHECK_FALSE(entry.has(String("headers")));
	CHECK(entry.get(String(MCP_MANAGED_MARKER)).asBool());
}

TEST_CASE("planMcpWrite creates a fresh file with our entry", "[mcp]")
{
	const McpWritePlan plan =
		planMcpWrite("", "http://127.0.0.1:6000/mcp", "tok");
	REQUIRE(plan.action == McpWriteAction::Write);
	const JsonValue entry = serverEntry(plan.content, MCP_SERVER_NAME);
	CHECK(entry.get("url").asString() == String("http://127.0.0.1:6000/mcp"));
	CHECK(entry.get(String(MCP_MANAGED_MARKER)).asBool());
	// whitespace-only input is treated as "no file yet" too
	CHECK(planMcpWrite("  \n\t ", "http://127.0.0.1:6000/mcp", "tok").action ==
		McpWriteAction::Write);
}

TEST_CASE("planMcpWrite updates OUR stale entry and keeps other servers", "[mcp]")
{
	// a prior session's marked entry plus a user-authored second server
	const std::string existing =
		"{\"mcpServers\":{"
		"\"orkige\":{\"type\":\"http\",\"url\":\"http://127.0.0.1:1/mcp\","
		"\"headers\":{\"Authorization\":\"Bearer old\"},"
		"\"x-orkige-managed\":true},"
		"\"other\":{\"type\":\"http\",\"url\":\"http://example/mcp\"}}}";
	const McpWritePlan plan =
		planMcpWrite(existing, "http://127.0.0.1:9999/mcp", "fresh");
	REQUIRE(plan.action == McpWriteAction::Write);
	// our entry is refreshed to the new url/token...
	const JsonValue ours = serverEntry(plan.content, "orkige");
	CHECK(ours.get("url").asString() == String("http://127.0.0.1:9999/mcp"));
	CHECK(ours.get("headers").get("Authorization").asString() ==
		String("Bearer fresh"));
	// ...and the user's other server is preserved verbatim
	const JsonValue other = serverEntry(plan.content, "other");
	CHECK(other.get("url").asString() == String("http://example/mcp"));
}

TEST_CASE("planMcpWrite SKIPS a foreign orkige server (never clobbered)", "[mcp]")
{
	// a user-authored server named orkige WITHOUT our marker: hands off
	const std::string existing =
		"{\"mcpServers\":{\"orkige\":{\"type\":\"http\","
		"\"url\":\"http://mine/mcp\"}}}";
	const McpWritePlan plan =
		planMcpWrite(existing, "http://127.0.0.1:9999/mcp", "tok");
	CHECK(plan.action == McpWriteAction::Skip);
}

TEST_CASE("planMcpWrite SKIPS an unparseable file", "[mcp]")
{
	const McpWritePlan plan =
		planMcpWrite("this is not json", "http://127.0.0.1:1/mcp", "t");
	CHECK(plan.action == McpWriteAction::Skip);
}

TEST_CASE("planMcpWrite adds our entry beside a user's non-orkige server", "[mcp]")
{
	const std::string existing =
		"{\"mcpServers\":{\"sentry\":{\"type\":\"http\","
		"\"url\":\"http://sentry/mcp\"}}}";
	const McpWritePlan plan =
		planMcpWrite(existing, "http://127.0.0.1:7/mcp", "tok");
	REQUIRE(plan.action == McpWriteAction::Write);
	CHECK(serverEntry(plan.content, "orkige").get(String(MCP_MANAGED_MARKER))
		.asBool());
	CHECK(serverEntry(plan.content, "sentry").get("url").asString() ==
		String("http://sentry/mcp"));
}

TEST_CASE("planMcpRemove deletes the file when orkige was the only server",
	"[mcp]")
{
	const McpWritePlan wrote =
		planMcpWrite("", "http://127.0.0.1:6000/mcp", "tok");
	REQUIRE(wrote.action == McpWriteAction::Write);
	const McpRemovePlan plan = planMcpRemove(wrote.content);
	CHECK(plan.action == McpRemoveAction::RemoveFile);
}

TEST_CASE("planMcpRemove rewrites (drops ours) when other servers remain",
	"[mcp]")
{
	const std::string existing =
		"{\"mcpServers\":{"
		"\"orkige\":{\"type\":\"http\",\"url\":\"http://127.0.0.1:1/mcp\","
		"\"x-orkige-managed\":true},"
		"\"other\":{\"type\":\"http\",\"url\":\"http://example/mcp\"}}}";
	const McpRemovePlan plan = planMcpRemove(existing);
	REQUIRE(plan.action == McpRemoveAction::Rewrite);
	const JsonValue servers = parseJson(plan.content).get("mcpServers");
	CHECK_FALSE(servers.has(String("orkige")));
	CHECK(servers.has(String("other")));
}

TEST_CASE("planMcpRemove LEAVES a foreign / absent orkige entry", "[mcp]")
{
	// foreign orkige (no marker)
	CHECK(planMcpRemove(
		"{\"mcpServers\":{\"orkige\":{\"type\":\"http\","
		"\"url\":\"http://mine/mcp\"}}}").action == McpRemoveAction::Leave);
	// no orkige at all
	CHECK(planMcpRemove(
		"{\"mcpServers\":{\"other\":{\"type\":\"http\","
		"\"url\":\"http://x/mcp\"}}}").action == McpRemoveAction::Leave);
	// empty / malformed
	CHECK(planMcpRemove("").action == McpRemoveAction::Leave);
	CHECK(planMcpRemove("nonsense").action == McpRemoveAction::Leave);
}
