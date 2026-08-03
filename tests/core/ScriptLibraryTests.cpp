/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	ScriptLibraryTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Tests of the LIBRARY LOADER - `script.require`, the mechanism that lets one
	project script use another as a library. Three halves: the PURE name jail
	and cycle message (core_script/ScriptLibrary.h, no filesystem, no backend),
	the Lua face driven through an injected ResourceReader - which is also the
	proof that a library resolves out of MOUNTED content and never through a
	raw file read - and the per-sandbox caching contract. The Lua half skips
	honestly under ORKIGE_SCRIPTING=OFF.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "CoreTestEnvironment.h"

#include <core_filesystem/ResourceReader.h>
#include <core_script/ScriptLibrary.h>
#include <core_script/ScriptRuntime.h>

#include <map>
#include <string>

namespace
{
	//! a fake in-test reader standing in for a MOUNTED pak/APK: it answers by
	//! resource name and has no file on disk behind it at all, so anything it
	//! satisfies provably did NOT come from an fopen
	class LibraryReader : public Orkige::ResourceReader
	{
	public:
		std::map<Orkige::String, Orkige::String>	files;
		mutable int									reads = 0;
		mutable std::map<Orkige::String, int>		readsByName;

		bool readText(Orkige::String const & name,
			Orkige::String & out) const override
		{
			++this->reads;
			++this->readsByName[name];
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

	//! RAII install/clear of the process-wide provider
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

TEST_CASE("ScriptLibrary::checkName jails a library name to the project",
	"[script][security]")
{
	Orkige::String error;

	// the legitimate shapes: a project-relative .lua anywhere in the tree
	CHECK(Orkige::ScriptLibrary::checkName("scripts/mathutil.lua", error));
	CHECK(Orkige::ScriptLibrary::checkName("scripts/lib/vec.lua", error));
	CHECK(Orkige::ScriptLibrary::checkName("tests/helpers.lua", error));

	// empty
	CHECK_FALSE(Orkige::ScriptLibrary::checkName("", error));
	CHECK(error.find("empty") != Orkige::String::npos);

	// ABSOLUTE paths - the escape a raw loadfile() would have granted
	CHECK_FALSE(Orkige::ScriptLibrary::checkName("/etc/evil.lua", error));
	CHECK_FALSE(Orkige::ScriptLibrary::checkName("C:/Windows/evil.lua", error));
	CHECK_FALSE(Orkige::ScriptLibrary::checkName("\\\\server\\share\\x.lua",
		error));

	// ".." traversal, in both separator spellings
	CHECK_FALSE(Orkige::ScriptLibrary::checkName("../evil.lua", error));
	CHECK_FALSE(Orkige::ScriptLibrary::checkName("scripts/../../evil.lua",
		error));
	CHECK_FALSE(Orkige::ScriptLibrary::checkName("..\\..\\evil.lua", error));

	// a name that merely STARTS with dots is a legal file name
	CHECK(Orkige::ScriptLibrary::checkName("scripts/..hidden.lua", error));

	// a LIBRARY is Lua source - the extension is what keeps the reachable set
	// exactly the set a path-bound ScriptComponent could already run
	CHECK_FALSE(Orkige::ScriptLibrary::checkName("scenes/level.oscene", error));
	CHECK(error.find(".lua") != Orkige::String::npos);
	CHECK_FALSE(Orkige::ScriptLibrary::checkName("scripts/player", error));
	CHECK_FALSE(Orkige::ScriptLibrary::checkName(".lua", error));
}

TEST_CASE("ScriptLibrary::cycleError names the cycle, not the history",
	"[script]")
{
	Orkige::StringVector chain;
	chain.push_back("scripts/boot.lua");
	chain.push_back("scripts/a.lua");
	chain.push_back("scripts/b.lua");

	const Orkige::String message =
		Orkige::ScriptLibrary::cycleError(chain, "scripts/a.lua");
	CHECK(message.find("circular library dependency") != Orkige::String::npos);
	// the chain is rendered from where the loop CLOSES
	CHECK(message.find("scripts/a.lua -> scripts/b.lua -> scripts/a.lua") !=
		Orkige::String::npos);
	CHECK(message.find("boot.lua") == Orkige::String::npos);

	// a self-require still reads as a cycle
	Orkige::StringVector self;
	self.push_back("scripts/a.lua");
	CHECK(Orkige::ScriptLibrary::cycleError(self, "scripts/a.lua").find(
		"scripts/a.lua -> scripts/a.lua") != Orkige::String::npos);
}

TEST_CASE("script.require loads a library out of MOUNTED content",
	"[script][security]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	if(!Orkige::ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - the loader has nothing to run");
		return;
	}
	Orkige::ScriptRuntime & runtime = env.scriptRuntime;

	LibraryReader reader;
	// there is NO such file on disk anywhere: a pass here can only mean the
	// load went through the resource reader (the pak/APK road)
	reader.files["scripts/mathutil.lua"] =
		"local M = {}\n"
		"function M.clamp(v, lo, hi)\n"
		"  if v < lo then return lo end\n"
		"  if v > hi then return hi end\n"
		"  return v\n"
		"end\n"
		"M.tag = 'from-the-archive'\n"
		"return M\n";
	InstalledReader installed(&reader);

	const Orkige::ScriptRuntime::Result result = runtime.runString(
		"local m = script.require('scripts/mathutil.lua')\n"
		"return m.clamp(-5, -1, 1), m.tag\n");
	REQUIRE(result.error.empty());
	REQUIRE(result.success);
	REQUIRE(result.returnValues.size() == 2);
	CHECK(result.returnValues[0] == "-1");
	CHECK(result.returnValues[1] == "from-the-archive");
	CHECK(reader.reads > 0);
}

TEST_CASE("script.require refuses an escape, a miss and a cycle",
	"[script][security]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	if(!Orkige::ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - the loader has nothing to run");
		return;
	}
	Orkige::ScriptRuntime & runtime = env.scriptRuntime;

	LibraryReader reader;
	// a MUTUAL dependency: without the cycle guard this recurses until the C
	// stack dies - the whole reason the guard exists
	reader.files["scripts/a.lua"] =
		"local b = script.require('scripts/b.lua')\nreturn { b = b }\n";
	reader.files["scripts/b.lua"] =
		"local a = script.require('scripts/a.lua')\nreturn { a = a }\n";
	InstalledReader installed(&reader);

	// (1) an absolute path never reaches the reader
	{
		const int readsBefore = reader.reads;
		const Orkige::ScriptRuntime::Result result = runtime.runString(
			"return script.require('/etc/passwd.lua')\n");
		CHECK_FALSE(result.success);
		CHECK(result.error.find("project-relative") != Orkige::String::npos);
		CHECK(reader.reads == readsBefore);
	}
	// (2) ".." traversal, likewise
	{
		const Orkige::ScriptRuntime::Result result = runtime.runString(
			"return script.require('../evil.lua')\n");
		CHECK_FALSE(result.success);
		CHECK(result.error.find("project-relative") != Orkige::String::npos);
	}
	// (3) a miss is an honest raise naming the library
	{
		const Orkige::ScriptRuntime::Result result = runtime.runString(
			"return script.require('scripts/absent.lua')\n");
		CHECK_FALSE(result.success);
		CHECK(result.error.find("scripts/absent.lua") != Orkige::String::npos);
		CHECK(result.error.find("not found") != Orkige::String::npos);
	}
	// (4) THE CYCLE: a named refusal, not a stack overflow
	{
		const Orkige::ScriptRuntime::Result result = runtime.runString(
			"return script.require('scripts/a.lua')\n");
		CHECK_FALSE(result.success);
		CHECK(result.error.find("circular library dependency") !=
			Orkige::String::npos);
		// and the runtime is still usable afterwards - the chain unwound
		reader.files["scripts/ok.lua"] = "return 7\n";
		const Orkige::ScriptRuntime::Result after = runtime.runString(
			"return script.require('scripts/ok.lua')\n");
		CHECK(after.success);
	}
	// (5) a library that fails to COMPILE names itself
	{
		reader.files["scripts/broken.lua"] = "this is not lua(\n";
		const Orkige::ScriptRuntime::Result result = runtime.runString(
			"return script.require('scripts/broken.lua')\n");
		CHECK_FALSE(result.success);
		CHECK(result.error.find("scripts/broken.lua") != Orkige::String::npos);
	}
}

TEST_CASE("script.require caches per sandbox and never per process",
	"[script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	if(!Orkige::ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - the loader has nothing to run");
		return;
	}
	Orkige::ScriptRuntime & runtime = env.scriptRuntime;

	LibraryReader reader;
	// the library counts its own loads in a table it keeps to itself, so a
	// second instance is observable
	reader.files["scripts/counter.lua"] =
		"local M = { hits = 0 }\n"
		"function M.bump() M.hits = M.hits + 1 return M.hits end\n"
		"return M\n";
	InstalledReader installed(&reader);

	// within ONE sandbox the same require yields the IDENTICAL table
	const Orkige::ScriptRuntime::Result same = runtime.runString(
		"local a = script.require('scripts/counter.lua')\n"
		"local b = script.require('scripts/counter.lua')\n"
		"a.bump()\n"
		"return tostring(a == b), tostring(b.hits)\n");
	REQUIRE(same.error.empty());
	REQUIRE(same.returnValues.size() == 2);
	CHECK(same.returnValues[0] == "true");
	CHECK(same.returnValues[1] == "1");

	// the whole require above cost exactly ONE read of the library
	CHECK(reader.readsByName["scripts/counter.lua"] == 1);

	// a DIFFERENT sandbox gets its OWN copy: one script instance's module
	// state can never leak into another's (deliberate sharing is the `shared`
	// table). Two instances of one script are two sandboxes, so the library
	// is read - and evaluated - once for each.
	reader.files["scripts/user.lua"] =
		"local c = script.require('scripts/counter.lua')\n"
		"loadedHits = c.bump()\n";
	Orkige::String loadError;
	Orkige::optr<Orkige::ScriptInstance> first =
		runtime.loadScriptInstance("scripts/user.lua", &loadError);
	REQUIRE(loadError.empty());
	REQUIRE(first);
	CHECK(reader.readsByName["scripts/counter.lua"] == 2);

	Orkige::optr<Orkige::ScriptInstance> second =
		runtime.loadScriptInstance("scripts/user.lua", &loadError);
	REQUIRE(loadError.empty());
	REQUIRE(second);
	// a THIRD read: had the cache been process-wide, this would have stayed 2
	// and the second instance would have shared the first's mutable table
	CHECK(reader.readsByName["scripts/counter.lua"] == 3);
}
