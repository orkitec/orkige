/**************************************************************
	created:	2026/07/20 at 12:00
	filename: 	ResourceReaderTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Tests of the backend-neutral core_filesystem/ResourceReader seam: the
	process-wide provider (ResourceAccess) and the ScriptRuntime routing that
	loads a script's source THROUGH an injected reader (the archive-in-place
	read) with a fall-back to the on-disk file when no reader is installed or a
	reader misses. Like ScriptRuntimeTests this compiles in EVERY scripting
	configuration: the seam must be inert-but-present in ORKIGE_SCRIPTING=OFF.
***************************************************************/

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "CoreTestEnvironment.h"

#include <core_filesystem/ResourceReader.h>
#include <core_script/ScriptRuntime.h>

#include <filesystem>
#include <fstream>
#include <map>

using Orkige::optr;

namespace
{
	//! a fake in-test reader: returns source by exact resource name from a map,
	//! counting the reads so a test can prove the reader was consulted. Missing
	//! names return false (the honest miss that makes the caller fall back).
	class FakeReader : public Orkige::ResourceReader
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
			if (it == this->files.end())
			{
				return false;	// a miss - out untouched, the caller falls back
			}
			out = it->second;
			return true;
		}
	};

	//! RAII: install a reader for the scope, always clear on exit so the
	//! process-wide provider never leaks into another [script] test (they share
	//! one ScriptRuntime singleton through CoreTestEnvironment)
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

	//! a throwaway directory with a scripts/ subfolder (mirror of the
	//! ScriptRuntimeTests helper) - for the disk-fallback legs
	struct TempScriptDir
	{
		std::filesystem::path root;
		explicit TempScriptDir(std::string const & name)
			: root(std::filesystem::temp_directory_path() / name)
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
			std::filesystem::create_directories(this->root / "scripts");
		}
		~TempScriptDir()
		{
			std::error_code ignored;
			std::filesystem::remove_all(this->root, ignored);
		}
		void write(std::string const & name, std::string const & source)
		{
			std::ofstream file(this->root / "scripts" / name);
			file << source;
		}
	};
}

TEST_CASE("ResourceAccess provider stores and clears a non-owning reader",
	"[filesystem]")
{
	// backend-neutral: this seam is ALWAYS compiled, even in ORKIGE_SCRIPTING=OFF
	CHECK(Orkige::ResourceAccess::reader() == nullptr);	// unset by default

	FakeReader fake;
	Orkige::ResourceAccess::setReader(&fake);
	CHECK(Orkige::ResourceAccess::reader() == &fake);

	// a second install replaces (non-owning - the provider never deletes)
	FakeReader other;
	Orkige::ResourceAccess::setReader(&other);
	CHECK(Orkige::ResourceAccess::reader() == &other);

	Orkige::ResourceAccess::setReader(nullptr);
	CHECK(Orkige::ResourceAccess::reader() == nullptr);	// cleared, honest
}

TEST_CASE("ScriptRuntime loads a script THROUGH the injected reader (in place)",
	"[filesystem][script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();

	// the search root is an EMPTY temp dir - there is NO file on disk for this
	// name, so a successful load PROVES the source came from the reader (memory),
	// the archive-in-place read that removes the fopen requirement
	TempScriptDir dir("orkige_resourcereader_inplace_test");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());
	env.scriptRuntime.ensureGlobalTable("seam_reader");

	FakeReader fake;
	fake.files["scripts/mem.lua"] = R"lua(
		seam_reader.loaded = (seam_reader.loaded or 0) + 1
		function init(self) seam_reader.inited = true end
		function update(self, dt) seam_reader.dt = dt end
		function shutdown(self) seam_reader.down = true end
	)lua";
	InstalledReader installed(&fake);

	Orkige::String error;
	optr<Orkige::ScriptInstance> instance =
		env.scriptRuntime.loadScriptInstance("scripts/mem.lua", &error);

	if (!Orkige::ScriptRuntime::available())
	{
		// OFF configuration: the seam is inert - the load is refused honestly,
		// the reader is never even consulted (loadScriptInstance short-circuits)
		CHECK(instance == nullptr);
		CHECK(error.find("scripting disabled") != Orkige::String::npos);
		env.scriptRuntime.setScriptSearchRoot("");
		return;
	}

	INFO("load error: " << error);
	REQUIRE(instance);
	CHECK(fake.reads >= 1);	// the reader WAS consulted (no disk file exists)
	REQUIRE(instance->callInit(&error));
	REQUIRE(instance->callUpdate(0.25f, &error));
	REQUIRE(instance->callShutdown(&error));
	CHECK(env.scriptRuntime.getNumber({"seam_reader", "loaded"}, -1.0) == 1.0);
	CHECK(env.scriptRuntime.getBool({"seam_reader", "inited"}, false));
	CHECK(env.scriptRuntime.getNumber({"seam_reader", "dt"}, -1.0) ==
		Catch::Approx(0.25));
	CHECK(env.scriptRuntime.getBool({"seam_reader", "down"}, false));

	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("ScriptRuntime falls back to the on-disk file when the reader is unset "
	"or misses", "[filesystem][script]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();
	if (!Orkige::ScriptRuntime::available())
	{
		SUCCEED("scripting disabled - the fall-back path is a Lua-only concern");
		return;
	}

	TempScriptDir dir("orkige_resourcereader_fallback_test");
	env.scriptRuntime.ensureGlobalTable("seam_fallback");
	dir.write("disk.lua",
		"seam_fallback.hits = (seam_fallback.hits or 0) + 1\n");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());

	// (a) NO reader installed -> the existing on-disk path loads the file
	{
		Orkige::String error;
		optr<Orkige::ScriptInstance> instance =
			env.scriptRuntime.loadScriptInstance("scripts/disk.lua", &error);
		INFO("no-reader load error: " << error);
		REQUIRE(instance);
	}
	CHECK(env.scriptRuntime.getNumber({"seam_fallback", "hits"}, -1.0) == 1.0);

	// (b) a reader installed but MISSING this name -> honest miss, disk fallback
	{
		FakeReader fake;
		fake.files["scripts/other.lua"] = "-- not the file we ask for\n";
		InstalledReader installed(&fake);

		Orkige::String error;
		optr<Orkige::ScriptInstance> instance =
			env.scriptRuntime.loadScriptInstance("scripts/disk.lua", &error);
		INFO("miss-fallback load error: " << error);
		REQUIRE(instance);
		CHECK(fake.reads == 1);	// consulted once, missed, fell back to disk
	}
	CHECK(env.scriptRuntime.getNumber({"seam_fallback", "hits"}, -1.0) == 2.0);

	env.scriptRuntime.setScriptSearchRoot("");
}

TEST_CASE("ScriptRuntime reads exported properties through the injected reader",
	"[filesystem][script][export]")
{
	Orkige::CoreTestEnvironment & env = Orkige::CoreTestEnvironment::get();

	// again an empty search root: the exports must come from the reader's memory
	TempScriptDir dir("orkige_resourcereader_export_test");
	env.scriptRuntime.setScriptSearchRoot(dir.root.string());

	FakeReader fake;
	fake.files["scripts/exp.lua"] = R"lua(
		properties = {
			speed = { type = "number", default = 3.0 },
			flag  = { type = "bool",   default = true },
		}
		function update(self, dt) end
	)lua";
	InstalledReader installed(&fake);

	std::vector<Orkige::ScriptExportProperty> exports =
		env.scriptRuntime.readExportedProperties("scripts/exp.lua");

	if (!Orkige::ScriptRuntime::available())
	{
		CHECK(exports.empty());	// OFF: no exports, honest
		env.scriptRuntime.setScriptSearchRoot("");
		return;
	}

	CHECK(fake.reads >= 1);	// the probe read the source through the reader
	REQUIRE(exports.size() == 2);
	CHECK(exports[0].name == "flag");	// deterministic name order
	CHECK(exports[1].name == "speed");

	env.scriptRuntime.setScriptSearchRoot("");
}
