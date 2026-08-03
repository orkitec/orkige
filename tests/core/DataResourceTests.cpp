/**************************************************************
	created:	2026/08/03 at 12:00
	filename: 	DataResourceTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Tests of core_filesystem/DataResource - the read a sandboxed script uses
	to get at AUTHORED DATA - plus its Lua face, the `data` table. Two halves:
	the PURE name guard (no reader, no filesystem) and the read, which must go
	through the injected ResourceReader and must NEVER fall back to a raw file
	read. Compiles in every scripting configuration: the C++ half is
	backend-neutral and the Lua half skips honestly under ORKIGE_SCRIPTING=OFF.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_filesystem/DataResource.h>
#include <core_filesystem/ResourceReader.h>
#include <core_script/ScriptRuntime.h>

#include <map>
#include <string>

namespace
{
	//! a fake in-test reader: returns content by exact resource name from a
	//! map; a missing name is the honest miss
	class MapReader : public Orkige::ResourceReader
	{
	public:
		std::map<Orkige::String, Orkige::String>	files;
		mutable int									reads = 0;

		bool readText(Orkige::String const & name,
			Orkige::String & out) const override
		{
			++this->reads;
			std::map<Orkige::String, Orkige::String>::const_iterator it =
				this->files.find(name);
			if(it == this->files.end())
			{
				return false;
			}
			out = it->second;
			return true;
		}
	};

	//! RAII install/clear of the process-wide provider (the singleton set is
	//! shared across test cases - never leak a reader into the next one)
	struct InstalledReader
	{
		explicit InstalledReader(Orkige::ResourceReader * reader)
		{
			Orkige::ResourceAccess::setReader(reader);
		}
		~InstalledReader()
		{
			Orkige::ResourceAccess::setReader(nullptr);
		}
	};
}

TEST_CASE("DataResource::checkName jails a data name to the project",
	"[filesystem][security]")
{
	// PURE: no reader, no filesystem - the guard is a decision about the name
	Orkige::String error;

	// legal project-relative names, whatever they hold: the guard is about
	// CONTAINMENT, never about formats or extensions
	CHECK(Orkige::DataResource::checkName("data/levels.json", error));
	CHECK(Orkige::DataResource::checkName("data/items.csv", error));
	CHECK(Orkige::DataResource::checkName("data/deep/nested/dialogue.txt",
		error));
	CHECK(Orkige::DataResource::checkName("assets/atlas.ogui", error));
	CHECK(Orkige::DataResource::checkName("tuning.ini", error));
	// a .lua file is READABLE as text - reading is not executing (the sandbox
	// keeps denying load/loadfile/dofile, which is what "executing" means)
	CHECK(Orkige::DataResource::checkName("scripts/player.lua", error));

	// empty
	error.clear();
	CHECK_FALSE(Orkige::DataResource::checkName("", error));
	CHECK_FALSE(error.empty());

	// ABSOLUTE paths - the machine is not addressable
	error.clear();
	CHECK_FALSE(Orkige::DataResource::checkName("/etc/passwd", error));
	CHECK_FALSE(error.empty());
	CHECK_FALSE(Orkige::DataResource::checkName("C:/Windows/system.ini",
		error));
	CHECK_FALSE(Orkige::DataResource::checkName("\\\\server\\share\\x",
		error));

	// TRAVERSAL - in every position, and with either separator
	CHECK_FALSE(Orkige::DataResource::checkName("../secret.json", error));
	CHECK_FALSE(Orkige::DataResource::checkName("data/../../secret.json",
		error));
	CHECK_FALSE(Orkige::DataResource::checkName("data/..", error));
	CHECK_FALSE(Orkige::DataResource::checkName("..\\..\\secret.json", error));

	// a name that merely STARTS with dots is a legal file name, not a traversal
	CHECK(Orkige::DataResource::checkName("data/..hidden.json", error));
}

TEST_CASE("DataResource::read goes through the reader, never a file",
	"[filesystem][security]")
{
	// (1) NO reader installed: an honest refusal, NOT a fopen fallback. This
	// is the whole point - a script must not gain a raw filesystem path when
	// the content mounts are down.
	{
		CHECK(Orkige::ResourceAccess::reader() == nullptr);
		Orkige::String text;
		Orkige::String error;
		CHECK_FALSE(Orkige::DataResource::read("data/levels.json", text,
			error));
		CHECK(text.empty());
		CHECK(error.find("no content reader") != Orkige::String::npos);
	}

	MapReader reader;
	reader.files["data/levels.json"] = "{\"levels\":3}";
	InstalledReader installed(&reader);

	// (2) a hit comes back verbatim, and it came from the READER
	Orkige::String text;
	Orkige::String error;
	REQUIRE(Orkige::DataResource::read("data/levels.json", text, error));
	CHECK(text == "{\"levels\":3}");
	CHECK(reader.reads == 1);

	// (3) a miss is honest
	text.clear();
	CHECK_FALSE(Orkige::DataResource::read("data/absent.json", text, error));
	CHECK(error.find("not found") != Orkige::String::npos);

	// (4) a refused NAME never even reaches the reader
	const int readsBefore = reader.reads;
	CHECK_FALSE(Orkige::DataResource::read("../levels.json", text, error));
	CHECK(reader.reads == readsBefore);
}

TEST_CASE("DataResource::read refuses content over the size cap",
	"[filesystem][security]")
{
	MapReader reader;
	reader.files["data/huge.txt"] =
		Orkige::String(Orkige::DataResource::kMaxBytes + 1, 'x');
	reader.files["data/atcap.txt"] =
		Orkige::String(Orkige::DataResource::kMaxBytes, 'x');
	InstalledReader installed(&reader);

	Orkige::String text;
	Orkige::String error;
	CHECK_FALSE(Orkige::DataResource::read("data/huge.txt", text, error));
	CHECK(text.empty());
	CHECK(error.find("limit") != Orkige::String::npos);

	// exactly at the cap is fine - the bound is inclusive
	CHECK(Orkige::DataResource::read("data/atcap.txt", text, error));
	CHECK(text.size() == Orkige::DataResource::kMaxBytes);
}

TEST_CASE("the Lua data table reads and decodes through the reader",
	"[filesystem][script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	if(!Orkige::ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - there is no Lua surface to read with");
		return;
	}

	MapReader reader;
	reader.files["data/levels.json"] =
		"{\"name\":\"world 1\",\"rooms\":[{\"id\":1,\"boss\":false},"
		"{\"id\":2,\"boss\":true}],\"gravity\":-20.5,\"note\":null}";
	reader.files["data/notes.txt"] = "plain text, not a format we police\n";
	reader.files["data/broken.json"] = "{not json";
	InstalledReader installed(&reader);

	// data.read is FORMAT-NEUTRAL: text comes back as the bytes on record
	Orkige::ScriptRuntime::Result result =
		env.scriptRuntime.runString("return data.read('data/notes.txt')");
	REQUIRE(result.success);
	REQUIRE(result.returnValues.size() == 1);
	CHECK(result.returnValues[0] == "plain text, not a format we police\n");

	// data.readJson decodes to natural Lua values - nesting, arrays (1-based),
	// numbers, booleans, and a JSON null reading as nil
	result = env.scriptRuntime.runString(R"lua(
		local levels, err = data.readJson('data/levels.json')
		assert(levels ~= nil, tostring(err))
		assert(levels.name == 'world 1', 'string member')
		assert(#levels.rooms == 2, 'array length')
		assert(levels.rooms[1].id == 1, 'array element member')
		assert(levels.rooms[2].boss == true, 'boolean member')
		assert(math.abs(levels.gravity + 20.5) < 1e-9, 'number member')
		assert(levels.note == nil, 'json null reads as nil')
		return 'ok'
	)lua");
	INFO(result.error);
	REQUIRE(result.success);

	// data.json decodes text a script already has - the same codec, no read
	result = env.scriptRuntime.runString(
		"local t = data.json('{\"a\":[1,2,3]}') "
		"assert(t.a[3] == 3) return 'ok'");
	INFO(result.error);
	REQUIRE(result.success);

	// every refusal is the SAME two-value shape: nil plus a reason
	result = env.scriptRuntime.runString(R"lua(
		local text, err = data.read('data/absent.json')
		assert(text == nil and type(err) == 'string', 'miss')
		local escape, escapeErr = data.read('../secret')
		assert(escape == nil and type(escapeErr) == 'string', 'traversal')
		local bad, badErr = data.readJson('data/broken.json')
		assert(bad == nil and type(badErr) == 'string', 'malformed json')
		local raw, rawErr = data.json('{not json')
		assert(raw == nil and type(rawErr) == 'string', 'malformed text')
		return 'ok'
	)lua");
	INFO(result.error);
	REQUIRE(result.success);

	// the table grants READING only: it opens no writes and no handles, and
	// the denied globals stay denied beside it
	result = env.scriptRuntime.runString(R"lua(
		assert(io == nil and loadfile == nil and dofile == nil)
		assert(data.write == nil and data.open == nil and data.remove == nil)
		return 'ok'
	)lua");
	INFO(result.error);
	REQUIRE(result.success);
}
