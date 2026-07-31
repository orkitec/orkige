/********************************************************************
	created:	Friday 2026/07/31 at 16:00
	filename: 	ExportSelfContainTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	Making a macOS binary inside a bundle self-contained.

	This is the step whose failure mode is invisible on the machine that built
	the app: with one build-tree rpath left in, a missing dylib still resolves
	HERE and dies in dyld on a user's machine. So the parsing and the decisions
	are pure and asserted directly, and the orchestration runs against an
	injected tool runner that records what would have been executed - the
	argument composition is checked without a Mach-O binary, an install_name_tool
	or a signing identity anywhere in sight.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportFiles.h"
#include "ExportSelfContain.h"

#include <filesystem>

using namespace OrkigeExport;

namespace
{
	struct ScratchDir
	{
		Orkige::String path;
		explicit ScratchDir(Orkige::String const & name)
		{
			this->path = (std::filesystem::temp_directory_path() /
				("orkige_selfcontain_test_" + name)).string();
			ExportFiles::removeTree(this->path, 0);
			ExportFiles::makeDirectories(this->path, 0);
		}
		~ScratchDir() { ExportFiles::removeTree(this->path, 0); }
	};

	//! a runner that answers the two otool queries from canned text and
	//! RECORDS every mutation instead of performing it
	struct RecordingRunner
	{
		Orkige::String								dependencies;
		Orkige::String								loadCommands;
		std::vector<std::vector<Orkige::String> >	calls;

		ProcessRunner runner()
		{
			return [this](std::vector<Orkige::String> const & arguments)
			{
				this->calls.push_back(arguments);
				ProcessResult result;
				result.launched = true;
				result.exitCode = 0;
				if(arguments.size() >= 2 && arguments[0] == "otool")
				{
					result.output = (arguments[1] == "-L")
						? this->dependencies : this->loadCommands;
				}
				return result;
			};
		}

		//! did any recorded call start with these arguments
		bool ran(std::vector<Orkige::String> const & prefix) const
		{
			for(std::vector<Orkige::String> const & call : this->calls)
			{
				if(call.size() < prefix.size())
				{
					continue;
				}
				bool match = true;
				for(std::size_t index = 0; index < prefix.size(); ++index)
				{
					match = match && call[index] == prefix[index];
				}
				if(match)
				{
					return true;
				}
			}
			return false;
		}
	};
}

TEST_CASE("otool -L yields the non-system dependencies", "[unit][export]")
{
	const Orkige::String output =
		"/build/OrkigePlayer:\n"
		"\t@rpath/libvulkan.1.dylib (compatibility version 1.0.0, "
		"current version 1.4.350)\n"
		"\t/opt/dev/libcustom.dylib (compatibility version 1.0.0)\n"
		"\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n"
		"\t/System/Library/Frameworks/Metal.framework/Versions/A/Metal "
		"(compatibility version 1.0.0)\n";

	const std::vector<Orkige::String> dependencies =
		parseOtoolDependencies(output);
	REQUIRE(dependencies.size() == 2);
	CHECK(dependencies[0] == "@rpath/libvulkan.1.dylib");
	CHECK(dependencies[1] == "/opt/dev/libcustom.dylib");
	// the OS's own libraries stay dynamic - bundling them would be both wrong
	// and, for a framework, unsigned
}

TEST_CASE("otool -l yields the rpaths", "[unit][export]")
{
	const Orkige::String output =
		"Load command 12\n"
		"          cmd LC_RPATH\n"
		"      cmdsize 48\n"
		"         path /build/vcpkg_installed/arm64-osx/lib (offset 12)\n"
		"Load command 13\n"
		"          cmd LC_RPATH\n"
		"      cmdsize 48\n"
		"         path @executable_path/../Frameworks (offset 12)\n"
		"Load command 14\n"
		"          cmd LC_LOAD_DYLIB\n"
		"         name /usr/lib/libSystem.B.dylib (offset 24)\n";

	const std::vector<Orkige::String> rpaths = parseOtoolRpaths(output);
	REQUIRE(rpaths.size() == 2);
	CHECK(rpaths[0] == "/build/vcpkg_installed/arm64-osx/lib");
	CHECK(rpaths[1] == "@executable_path/../Frameworks");
	// a dylib load command is not an rpath
}

TEST_CASE("build-machine rpaths are recognised", "[unit][export]")
{
	const std::vector<Orkige::String> banned =
		{ "vcpkg_installed", "/Users/dev/orkige" };
	CHECK(isBuildMachineRpath("/x/vcpkg_installed/arm64-osx/lib", banned));
	CHECK(isBuildMachineRpath("/Users/dev/orkige/build/lib", banned));
	// the bundle's own rpath is what the shipped binary must keep
	CHECK_FALSE(isBuildMachineRpath("@executable_path/../Frameworks", banned));
	CHECK_FALSE(isBuildMachineRpath("@loader_path/../Frameworks", banned));
	CHECK_FALSE(isBuildMachineRpath("/opt/homebrew/lib", banned));
	// an empty marker must not match everything
	CHECK_FALSE(isBuildMachineRpath("/anything", { "" }));
}

TEST_CASE("a versioned dylib's loader aliases are found", "[unit][export]")
{
	ScratchDir scratch("aliases");
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(scratch.path, "libfoo.1.2.3.dylib"), "x", 0));
	REQUIRE(ExportFiles::makeSymlink("libfoo.1.2.3.dylib",
		ExportFiles::join(scratch.path, "libfoo.dylib"), 0));
	REQUIRE(ExportFiles::makeSymlink("libfoo.1.2.3.dylib",
		ExportFiles::join(scratch.path, "libfoo.1.dylib"), 0));
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(scratch.path, "libbar.dylib"), "x", 0));
	REQUIRE(ExportFiles::makeSymlink("libbar.dylib",
		ExportFiles::join(scratch.path, "libbar.1.dylib"), 0));

	// a leaf-name dlopen asks for the unversioned spellings, so the bundle
	// must carry them beside the real file
	const std::vector<Orkige::String> aliases =
		dylibAliases(scratch.path, "libfoo.1.2.3.dylib");
	REQUIRE(aliases.size() == 2);
	CHECK(aliases[0] == "libfoo.1.dylib");
	CHECK(aliases[1] == "libfoo.dylib");
	// an unrelated library's aliases stay its own
	CHECK(dylibAliases(scratch.path, "libbar.dylib") ==
		std::vector<Orkige::String>{ "libbar.1.dylib" });
	// the real file is not its own alias
	CHECK(dylibAliases(scratch.path, "libfoo.dylib").empty() == false);
}

TEST_CASE("a dependency resolves against the search directories",
	"[unit][export]")
{
	ScratchDir scratch("resolve");
	const Orkige::String lib = ExportFiles::join(scratch.path, "lib");
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(lib, "libvulkan.1.dylib"), "x", 0));

	CHECK(resolveDylibDependency("@rpath/libvulkan.1.dylib", { lib }) ==
		ExportFiles::join(lib, "libvulkan.1.dylib"));
	// the FIRST directory that has it wins, so a build tree's own rpaths take
	// precedence over the fallbacks the caller adds
	CHECK(resolveDylibDependency("@rpath/libvulkan.1.dylib",
		{ "/nowhere", lib }) == ExportFiles::join(lib, "libvulkan.1.dylib"));
	CHECK(resolveDylibDependency("@rpath/libmissing.dylib", { lib }).empty());

	// an absolute path stands for itself
	const Orkige::String absolute = ExportFiles::join(lib, "libvulkan.1.dylib");
	CHECK(resolveDylibDependency(absolute, {}) == absolute);
	CHECK(resolveDylibDependency("/nowhere/libx.dylib", {}).empty());
}

TEST_CASE("self-contain bundles the closure and cleans the rpaths",
	"[unit][export]")
{
	ScratchDir scratch("run");
	const Orkige::String vcpkgLib =
		ExportFiles::join(scratch.path, "build/vcpkg_installed/arm64-osx/lib");
	REQUIRE(ExportFiles::writeTextFile(
		ExportFiles::join(vcpkgLib, "libvulkan.1.4.350.dylib"), "binary", 0));
	REQUIRE(ExportFiles::makeSymlink("libvulkan.1.4.350.dylib",
		ExportFiles::join(vcpkgLib, "libvulkan.1.dylib"), 0));
	const Orkige::String executable =
		ExportFiles::join(scratch.path, "App.app/Contents/MacOS/Game");
	REQUIRE(ExportFiles::writeTextFile(executable, "macho", 0));
	const Orkige::String frameworks =
		ExportFiles::join(scratch.path, "App.app/Contents/Frameworks");

	RecordingRunner recording;
	recording.dependencies =
		"/build/Game:\n"
		"\t@rpath/libvulkan.1.4.350.dylib (compatibility version 1.0.0)\n"
		"\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n";
	recording.loadCommands =
		"          cmd LC_RPATH\n"
		"         path " + vcpkgLib + " (offset 12)\n";

	SelfContainRequest request;
	request.executable = executable;
	request.frameworksDirectory = frameworks;
	request.searchDirectories = { vcpkgLib };
	request.bannedRpathMarkers = { "vcpkg_installed" };

	std::vector<Orkige::String> log;
	Orkige::String error;
	REQUIRE(makeSelfContained(request, recording.runner(),
		[&log](Orkige::String const & line) { log.push_back(line); }, &error));

	// the real dylib rode into the bundle...
	CHECK(ExportFiles::isRegularFile(
		ExportFiles::join(frameworks, "libvulkan.1.4.350.dylib")));
	// ...and so did its unversioned dlopen alias, still as a symlink
	CHECK(std::filesystem::is_symlink(std::filesystem::path(
		ExportFiles::join(frameworks, "libvulkan.1.dylib"))));

	// the build-tree rpath is DELETED: leaving it in makes a missing dylib
	// resolve on this machine and fail on the user's
	CHECK(recording.ran({ "install_name_tool", "-delete_rpath", vcpkgLib }));
	// and the bundle's own rpath is added in its place
	CHECK(recording.ran({ "install_name_tool", "-add_rpath",
		"@executable_path/../Frameworks" }));
	// install_name_tool invalidates the signature, so an ad-hoc re-sign closes
	// the pass - without it arm64 macOS refuses to run the binary at all
	CHECK(recording.ran({ "codesign", "--force", "-s", "-", executable }));
	// an @rpath dependency needs no -change; only an absolute one does
	CHECK_FALSE(recording.ran({ "install_name_tool", "-change" }));
}

TEST_CASE("an absolute dependency is retargeted at the bundle",
	"[unit][export]")
{
	ScratchDir scratch("absolute");
	const Orkige::String devLib = ExportFiles::join(scratch.path, "devlib");
	const Orkige::String dylib =
		ExportFiles::join(devLib, "libcustom.dylib");
	REQUIRE(ExportFiles::writeTextFile(dylib, "binary", 0));
	const Orkige::String executable =
		ExportFiles::join(scratch.path, "Game");
	REQUIRE(ExportFiles::writeTextFile(executable, "macho", 0));

	RecordingRunner recording;
	recording.dependencies = "/build/Game:\n\t" + dylib +
		" (compatibility version 1.0.0)\n";
	recording.loadCommands = "";

	SelfContainRequest request;
	request.executable = executable;
	request.frameworksDirectory =
		ExportFiles::join(scratch.path, "Frameworks");
	request.bannedRpathMarkers = { "vcpkg_installed" };

	Orkige::String error;
	REQUIRE(makeSelfContained(request, recording.runner(), nullptr, &error));
	// an absolute dev path must be rewritten to load out of the bundle
	CHECK(recording.ran({ "install_name_tool", "-change", dylib,
		"@rpath/libcustom.dylib" }));
	CHECK(recording.ran({ "install_name_tool", "-add_rpath",
		"@executable_path/../Frameworks" }));
}

TEST_CASE("an unresolvable dependency refuses", "[unit][export]")
{
	ScratchDir scratch("unresolved");
	const Orkige::String executable = ExportFiles::join(scratch.path, "Game");
	REQUIRE(ExportFiles::writeTextFile(executable, "macho", 0));

	RecordingRunner recording;
	recording.dependencies =
		"/build/Game:\n\t@rpath/libghost.dylib (compatibility version 1.0)\n";

	SelfContainRequest request;
	request.executable = executable;
	request.frameworksDirectory =
		ExportFiles::join(scratch.path, "Frameworks");

	Orkige::String error;
	// refusing here is the whole point: the alternative is an app that dies in
	// dyld on someone else's machine
	CHECK_FALSE(makeSelfContained(request, recording.runner(), nullptr,
		&error));
	CHECK(error.find("libghost.dylib") != Orkige::String::npos);
}

TEST_CASE("a binary with no closure changes nothing", "[unit][export]")
{
	ScratchDir scratch("static");
	const Orkige::String executable = ExportFiles::join(scratch.path, "Game");
	REQUIRE(ExportFiles::writeTextFile(executable, "macho", 0));

	RecordingRunner recording;
	recording.dependencies =
		"/build/Game:\n\t/usr/lib/libSystem.B.dylib (compatibility 1.0)\n";
	recording.loadCommands = "";

	SelfContainRequest request;
	request.executable = executable;
	request.frameworksDirectory =
		ExportFiles::join(scratch.path, "Frameworks");
	request.bannedRpathMarkers = { "vcpkg_installed" };

	Orkige::String error;
	REQUIRE(makeSelfContained(request, recording.runner(), nullptr, &error));
	// a fully statically linked flavor has no closure: no Frameworks
	// directory, no re-sign, nothing touched
	CHECK_FALSE(ExportFiles::exists(request.frameworksDirectory));
	CHECK_FALSE(recording.ran({ "codesign" }));
	CHECK_FALSE(recording.ran({ "install_name_tool" }));
}
