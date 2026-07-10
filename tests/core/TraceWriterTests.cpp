/**************************************************************
	created:	2026/07/10 at 14:00
	filename: 	TraceWriterTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/

//! @file TraceWriterTests.cpp
//! @brief unit coverage for the flight-recorder trace serializer: every
//! emitted line must be valid JSON with the documented fields, the per-sample
//! object cap must mark the real total, and the byte cap must truncate with an
//! honest marker line. Lines are validated by parsing them back through the
//! engine's own JsonValue parser.

#include <catch2/catch_test_macros.hpp>

#include "core_debugnet/TraceWriter.h"
#include "core_debugnet/Json.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace Orkige;

namespace
{
	//! split a .jsonl blob into its lines (dropping the trailing empty one)
	std::vector<String> splitLines(String const & text)
	{
		std::vector<String> lines;
		std::istringstream stream(text);
		std::string line;
		while (std::getline(stream, line))
		{
			if (!line.empty())
			{
				lines.push_back(line);
			}
		}
		return lines;
	}
}

TEST_CASE("TraceWriter emits parseable sample + event lines", "[unit][trace]")
{
	TraceWriter writer;

	TraceWriter::ObjectSample player;
	player.id = "Player";
	player.name = "Player";
	player.pos[0] = 1.5f; player.pos[1] = 2.0f; player.pos[2] = -3.25f;
	player.hasVelocity = true;
	player.vel[0] = 0.5f; player.vel[1] = 0.0f; player.vel[2] = 0.0f;
	player.active = true;
	player.visible = 1;

	TraceWriter::ObjectSample wall;
	wall.id = "Wall";
	wall.name = "Wall";
	wall.pos[0] = 10.0f;
	wall.active = false;
	wall.visible = 0;
	// no velocity (static, no rigid body) -> the field must be omitted

	writer.addSample(0.0, 0, 0.016, { player, wall });
	writer.addEvent(0.25, 15, "contactBegin",
		{ { "a", "Player" }, { "b", "Wall" } });

	std::vector<String> lines = splitLines(writer.buffer());
	REQUIRE(lines.size() == 2);

	// the sample line
	JsonValue sample;
	REQUIRE(JsonValue::parse(lines[0], sample));
	REQUIRE(sample.isObject());
	REQUIRE(sample.get("frame").asNumber(-1) == 0);
	REQUIRE(sample.get("dt").asNumber(-1) > 0.0);
	REQUIRE_FALSE(sample.has("mem"));	// no memRss given -> field omitted
	JsonValue const & objects = sample.get("objects");
	REQUIRE(objects.size() == 2);
	JsonValue const & first = objects.at(0);
	REQUIRE(first.get("id").asString() == "Player");
	REQUIRE(first.get("pos").size() == 3);
	REQUIRE(first.get("pos").at(0).asNumber(0) == 1.5);
	REQUIRE(first.get("vel").size() == 3);	// rigid body -> velocity present
	REQUIRE(first.get("active").asNumber(-1) == 1);
	REQUIRE(first.get("visible").asNumber(-1) == 1);
	JsonValue const & second = objects.at(1);
	REQUIRE(second.get("active").asNumber(-1) == 0);
	REQUIRE_FALSE(second.has("vel"));	// no rigid body -> omitted

	// the event line
	JsonValue event;
	REQUIRE(JsonValue::parse(lines[1], event));
	REQUIRE(event.get("event").asString() == "contactBegin");
	REQUIRE(event.get("frame").asNumber(-1) == 15);
	REQUIRE(event.get("a").asString() == "Player");
	REQUIRE(event.get("b").asString() == "Wall");
}

TEST_CASE("TraceWriter writes the process memory footprint", "[unit][trace]")
{
	TraceWriter writer;
	TraceWriter::ObjectSample object;
	object.id = "Ball";
	object.name = "Ball";
	// a realistic resident set size: large enough that the compact float
	// format WOULD have rounded it (200 MB), proving the exact-integer path
	const long long memRss = 209715200LL;	// 200 * 1024 * 1024
	writer.addSample(0.5, 30, 0.016, { object }, memRss);

	std::vector<String> lines = splitLines(writer.buffer());
	REQUIRE(lines.size() == 1);
	JsonValue sample;
	REQUIRE(JsonValue::parse(lines[0], sample));
	REQUIRE(sample.has("mem"));
	// the exact byte count survives (no float rounding)
	REQUIRE(sample.get("mem").asNumber(-1) == static_cast<double>(memRss));
}

TEST_CASE("TraceWriter caps the per-sample object list", "[unit][trace]")
{
	TraceWriter writer(TraceWriter::DEFAULT_MAX_BYTES, /*maxObjects*/ 3);
	std::vector<TraceWriter::ObjectSample> many;
	for (int i = 0; i < 10; ++i)
	{
		TraceWriter::ObjectSample object;
		object.id = "obj" + std::to_string(i);
		object.name = object.id;
		many.push_back(object);
	}
	writer.addSample(1.0, 5, 0.02, many);

	std::vector<String> lines = splitLines(writer.buffer());
	REQUIRE(lines.size() == 1);
	JsonValue sample;
	REQUIRE(JsonValue::parse(lines[0], sample));
	REQUIRE(sample.get("objects").size() == 3);		// capped
	REQUIRE(sample.get("capped").asNumber(-1) == 10);	// honest total
}

TEST_CASE("TraceWriter escapes strings and quotes", "[unit][trace]")
{
	TraceWriter writer;
	writer.addEvent(0.1, 1, "warning",
		{ { "message", "bad \"json\"\n\tand a backslash \\" } });
	std::vector<String> lines = splitLines(writer.buffer());
	REQUIRE(lines.size() == 1);
	JsonValue event;
	REQUIRE(JsonValue::parse(lines[0], event));	// escaping produced valid JSON
	REQUIRE(event.get("message").asString() ==
		"bad \"json\"\n\tand a backslash \\");
}

TEST_CASE("TraceWriter truncates at the byte cap with a marker", "[unit][trace]")
{
	// a tiny cap so a handful of samples overflow it
	TraceWriter writer(/*maxBytes*/ 512);
	TraceWriter::ObjectSample object;
	object.id = "Cube";
	object.name = "Cube";
	for (int i = 0; i < 200 && !writer.truncated(); ++i)
	{
		object.pos[0] = static_cast<float>(i);
		writer.addSample(i * 0.016, static_cast<unsigned long>(i), 0.016,
			{ object });
	}
	REQUIRE(writer.truncated());
	REQUIRE(writer.byteCount() <= 512);

	const std::string path =
		(std::filesystem::temp_directory_path() /
			"orkige_trace_truncate_test.jsonl").string();
	std::error_code cleanup;
	std::filesystem::remove(path, cleanup);
	REQUIRE(writer.save(path));

	std::ifstream file(path);
	std::vector<String> lines;
	std::string raw;
	while (std::getline(file, raw))
	{
		if (!raw.empty())
		{
			lines.push_back(raw);
		}
	}
	REQUIRE(lines.size() >= 2);
	// every retained line is valid JSON
	for (String const & line : lines)
	{
		JsonValue parsed;
		REQUIRE(JsonValue::parse(line, parsed));
	}
	// the last line is the truncation marker
	JsonValue marker;
	REQUIRE(JsonValue::parse(lines.back(), marker));
	REQUIRE(marker.get("truncated").asNumber(0) == 1);

	std::filesystem::remove(path, cleanup);
}
