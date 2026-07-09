/**************************************************************
	created:	2026/07/09 at 12:00
	filename: 	JsonTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_debugnet/Json.h>

using Orkige::JsonValue;
using Orkige::String;

TEST_CASE("JsonValue round-trips nested objects and arrays", "[json]")
{
	// build a JSON-RPC-shaped value: nested objects, an array of objects,
	// strings, numbers and booleans - exactly what the flat DebugMessage codec
	// cannot carry and what the MCP endpoint needs
	JsonValue request = JsonValue::object();
	request.set("jsonrpc", JsonValue("2.0"));
	request.set("id", JsonValue(7));
	request.set("method", JsonValue("tools/call"));
	JsonValue params = JsonValue::object();
	params.set("name", JsonValue("create_object"));
	JsonValue arguments = JsonValue::object();
	arguments.set("id", JsonValue("Cube"));
	arguments.set("position", JsonValue("1 2 3"));
	arguments.set("visible", JsonValue(true));
	params.set("arguments", arguments);
	request.set("params", params);
	JsonValue tags = JsonValue::array();
	tags.push(JsonValue("a"));
	tags.push(JsonValue("b"));
	request.set("tags", tags);

	const String encoded = request.serialize();
	// framing: a single line (no raw newlines leak in)
	CHECK(encoded.find('\n') == String::npos);

	JsonValue decoded;
	REQUIRE(JsonValue::parse(encoded, decoded));
	CHECK(decoded.serialize() == encoded);	// stable, order-preserving

	CHECK(decoded.get("jsonrpc").asString() == "2.0");
	CHECK(decoded.get("id").asInt() == 7);
	CHECK(decoded.get("method").asString() == "tools/call");
	CHECK(decoded.get("params").get("name").asString() == "create_object");
	CHECK(decoded.get("params").get("arguments").get("id").asString() == "Cube");
	CHECK(decoded.get("params").get("arguments").get("position").asString() ==
		"1 2 3");
	CHECK(decoded.get("params").get("arguments").get("visible").asBool());
	REQUIRE(decoded.get("tags").isArray());
	REQUIRE(decoded.get("tags").size() == 2);
	CHECK(decoded.get("tags").at(0).asString() == "a");
	CHECK(decoded.get("tags").at(1).asString() == "b");
}

TEST_CASE("JsonValue parses scalars, numbers and escapes", "[json]")
{
	JsonValue value;
	REQUIRE(JsonValue::parse(
		"{ \"n\" : -42.5e1 , \"i\" : 12 , \"b\" : false , \"z\" : null , "
		"\"s\" : \"a\\\"b\\n\\u00e9\" }", value));
	CHECK(value.get("n").asNumber() == -425.0);
	CHECK(value.get("i").asInt() == 12);
	CHECK(value.get("i").serialize() == "12");	// integral prints cleanly
	CHECK_FALSE(value.get("b").asBool());
	CHECK(value.get("z").isNull());
	CHECK(value.get("s").asString() == "a\"b\n\xc3\xa9");

	// a bare top-level array and scalar are valid JSON documents
	JsonValue array;
	REQUIRE(JsonValue::parse("[1,\"two\",true,[4]]", array));
	REQUIRE(array.isArray());
	CHECK(array.size() == 4);
	CHECK(array.at(3).isArray());
	CHECK(array.at(3).at(0).asInt() == 4);

	JsonValue scalar;
	REQUIRE(JsonValue::parse("  \"lonely\"  ", scalar));
	CHECK(scalar.asString() == "lonely");
}

TEST_CASE("JsonValue::parse rejects malformed input without crashing", "[json]")
{
	const char* malformed[] = {
		"",
		"   ",
		"garbage",
		"{",
		"}",
		"{\"a\":}",
		"{\"a\":1,}",
		"{\"a\" 1}",			// missing colon
		"{\"a\":1}x",			// trailing garbage
		"{\"a\":1}{\"b\":2}",	// two documents
		"[1,2",					// unterminated array
		"[1,,2]",				// empty element
		"{\"a\":\"\\q\"}",		// unknown escape
		"{\"a\":\"\\u12\"}",		// short unicode escape
		"{\"a\":bareword}",		// invalid bare scalar
		"{\"a\":12x4}",			// invalid number
		"{1:2}",				// non-string key
		"nul",					// truncated literal
	};
	for (const char* line : malformed)
	{
		INFO("input: " << line);
		JsonValue out;
		out = JsonValue(String("sentinel"));
		CHECK_FALSE(JsonValue::parse(line, out));
		// a failed parse leaves the target untouched
		CHECK(out.asString() == "sentinel");
	}

	// deeply nested input must fail cleanly (the depth guard), not crash
	String deep;
	for (int i = 0; i < 5000; ++i) deep += '[';
	JsonValue out;
	CHECK_FALSE(JsonValue::parse(deep, out));
}
