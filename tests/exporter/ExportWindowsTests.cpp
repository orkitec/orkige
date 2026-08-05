/********************************************************************
	created:	Wednesday 2026/08/05 at 22:30
	filename: 	ExportWindowsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	The decisions behind the Windows package: what the artifact is called, what
	rides beside the binary, and which host may produce one at all.

	Every one of them is a pure function, which is what lets a Mac assert them.
	The packaging itself needs a Windows player binary and is judged by the
	export_windows integration leg on a Windows machine; what CAN be decided
	without one is decided here, so a mistake in the naming or the refusal
	wording is caught long before a Windows runner sees it.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportRun.h"
#include "ExportSettings.h"
#include "ExportWindows.h"

#include <vector>

using namespace OrkigeExport;

namespace
{
	ExportProject projectNamed(Orkige::String const & name)
	{
		ExportProject project;
		project.root = "/work/games/thing";
		project.name = name;
		return project;
	}
}

TEST_CASE("windows is a platform the exporter packages", "[unit][export]")
{
	CHECK(isPackagedPlatform("windows"));
	CHECK(isDesktopPlatform("windows"));
	// the three desktops are one category, and the platforms whose artifact is
	// built around somebody else's binary are not in it
	CHECK(isDesktopPlatform("macos"));
	CHECK(isDesktopPlatform("linux"));
	CHECK_FALSE(isDesktopPlatform("ios-simulator"));
	CHECK_FALSE(isDesktopPlatform("android"));
	CHECK_FALSE(isDesktopPlatform("web"));
	CHECK_FALSE(isDesktopPlatform(""));
	// the texture cook reads the sidecar's DEFAULT block for a desktop, the
	// same one macOS and Linux get - a desktop GPU wants no mobile container
	CHECK(cookPlatformToken("windows") == "");
	CHECK(cookPlatformToken("windows") == cookPlatformToken("macos"));
	CHECK(cookPlatformToken("windows") == cookPlatformToken("linux"));
	// a desktop package's payload is loose files, so its own suite can ride
	// along and be DISCOVERED by the walk the runner does
	CHECK(testRunPlatformRefusal("windows").empty());
}

TEST_CASE("each desktop platform has a name a person reads", "[unit][export]")
{
	CHECK(desktopPlatformLabel("windows") == "Windows");
	CHECK(desktopPlatformLabel("macos") == "macOS");
	CHECK(desktopPlatformLabel("linux") == "Linux");
	// asked about something that is not a desktop, it says nothing rather than
	// inventing a label a refusal would then print
	CHECK(desktopPlatformLabel("android").empty());
	CHECK(desktopPlatformLabel("").empty());
}

TEST_CASE("the windows artifact is one name, usable on a command line",
	"[unit][export]")
{
	// the directory a person cds into and the executable inside it carry the
	// SAME name, and it is the executable name rather than the display one: a
	// display name may hold spaces, and a game reached by typing its path
	// should not need quoting
	CHECK(windowsAppDirectoryName(projectNamed("My Game 2")) == "MyGame2");
	CHECK(windowsExecutableName(projectNamed("My Game 2")) == "MyGame2.exe");
	CHECK(windowsAppDirectoryName(projectNamed("My Game 2")) ==
		projectNamed("My Game 2").exeName());
	// a name with nothing to keep still yields a runnable directory
	CHECK(windowsAppDirectoryName(projectNamed("---")) == "OrkigeGame");
	CHECK(windowsExecutableName(projectNamed("---")) == "OrkigeGame.exe");
	// the extension is what makes the file executable on Windows - there is no
	// permission bit to set, so it is never optional
	CHECK(windowsExecutableName(projectNamed("Roller")) == "Roller.exe");
}

TEST_CASE("a project named after a DOS device still packages",
	"[unit][export]")
{
	// these name character devices at every directory on Windows, so neither a
	// file nor a directory can carry one - and the extension does not save it,
	// because CON.exe is still the console. A project called "Aux" would
	// otherwise produce an artifact that cannot be WRITTEN.
	for(const char * reserved :
		{ "CON", "PRN", "AUX", "NUL", "COM1", "COM9", "LPT1", "LPT9" })
	{
		CHECK(isWindowsReservedName(reserved));
	}
	// the reservation is case-insensitive, and so is the check
	CHECK(isWindowsReservedName("con"));
	CHECK(isWindowsReservedName("Aux"));
	CHECK(isWindowsReservedName("Com3"));
	// ...and it is exactly those eighteen names: a longer name that merely
	// starts with one is an ordinary file, and the zero forms are not reserved
	CHECK_FALSE(isWindowsReservedName("CONSOLE"));
	CHECK_FALSE(isWindowsReservedName("AUXILIARY"));
	CHECK_FALSE(isWindowsReservedName("COM0"));
	CHECK_FALSE(isWindowsReservedName("LPT0"));
	CHECK_FALSE(isWindowsReservedName("COM10"));
	CHECK_FALSE(isWindowsReservedName("COM"));
	CHECK_FALSE(isWindowsReservedName(""));
	CHECK_FALSE(isWindowsReservedName("Roller"));

	// the escape keeps the name alphanumeric, so the result is still something
	// a person types without quoting - and the directory and the executable
	// move together, because a directory named CON is refused just as hard
	CHECK(windowsAppDirectoryName(projectNamed("Con")) == "ConGame");
	CHECK(windowsExecutableName(projectNamed("Con")) == "ConGame.exe");
	CHECK(windowsAppDirectoryName(projectNamed("Aux")) == "AuxGame");
	// a name that only REDUCES to a reserved one is caught too: the reduction
	// to alphanumerics happens first
	CHECK(windowsAppDirectoryName(projectNamed("L.P.T.1")) == "LPT1Game");
	// and an ordinary name is untouched
	CHECK(windowsAppDirectoryName(projectNamed("Console")) == "Console");
}

TEST_CASE("what rides beside the windows binary is enumerated, not swept",
	"[unit][export]")
{
	// the closure is linked statically, so the expected answer is NOTHING -
	// this is what asserts that a package which bundles no library did so by
	// decision rather than because a copy step was forgotten
	CHECK(windowsCompanionLibraries({}).empty());
	CHECK(windowsCompanionLibraries({ "orkige_player.exe" }).empty());

	// a build directory is full of things that are not the program. None of
	// them belong to the game: an import library and an export file are link
	// inputs, a .pdb is a developer's symbols (large, and it hands out every
	// name in the binary), .ilk is incremental-link state, and another
	// target's executable is another program.
	const std::vector<Orkige::String> messyTree = {
		"orkige_player.exe",
		"orkige_player.pdb",
		"orkige_player.ilk",
		"orkige_player.lib",
		"orkige_player.exp",
		"orkige_editor.exe",
		"CMakeCache.txt",
	};
	CHECK(windowsCompanionLibraries(messyTree).empty());

	// ...and when something IS a DLL it travels, because a package missing one
	// does not start at all. Order is the order given, so the copy is
	// deterministic.
	const std::vector<Orkige::String> withLibraries = {
		"orkige_player.exe",
		"orkige_player.pdb",
		"SDL3.dll",
		"OgreMain.dll",
		"zlib1.dll",
	};
	const std::vector<Orkige::String> carried =
		windowsCompanionLibraries(withLibraries);
	REQUIRE(carried.size() == 3);
	CHECK(carried[0] == "SDL3.dll");
	CHECK(carried[1] == "OgreMain.dll");
	CHECK(carried[2] == "zlib1.dll");

	// a linker may spell the extension either way, and the file system does
	// not care - so neither does this
	const std::vector<Orkige::String> shouty =
		windowsCompanionLibraries({ "SDL3.DLL", "Ogre.Dll" });
	CHECK(shouty.size() == 2);

	// a name that merely CONTAINS the letters is not a library
	CHECK(windowsCompanionLibraries({ "dll", ".dll", "notadllfile" }).empty());
}

TEST_CASE("a windows package is refused off its own host", "[unit][export]")
{
	// the host that CAN package says nothing - a refusal is what is asserted,
	// so an empty answer is the permission
	CHECK(desktopHostRefusal("windows", "windows").empty());

	// the cross cases: the refusal names the platform asked for, the host that
	// was asked, and the reason - a person reading it must learn what to do
	const Orkige::String fromMac = desktopHostRefusal("windows", "macos");
	REQUIRE_FALSE(fromMac.empty());
	CHECK(fromMac.find("Windows") != Orkige::String::npos);
	CHECK(fromMac.find("macOS") != Orkige::String::npos);
	CHECK(fromMac.find("cross-compile") != Orkige::String::npos);

	const Orkige::String fromLinux = desktopHostRefusal("windows", "linux");
	REQUIRE_FALSE(fromLinux.empty());
	CHECK(fromLinux.find("Windows") != Orkige::String::npos);
	CHECK(fromLinux.find("Linux") != Orkige::String::npos);

	// ...and the other direction, which is the half a Windows host sees. The
	// bug this guards is a label lookup that has run out of cases and calls
	// every unknown desktop "Linux".
	const Orkige::String macFromWindows = desktopHostRefusal("macos", "windows");
	REQUIRE_FALSE(macFromWindows.empty());
	CHECK(macFromWindows.find("macOS") != Orkige::String::npos);
	CHECK(macFromWindows.find("Windows") != Orkige::String::npos);

	const Orkige::String linuxFromWindows =
		desktopHostRefusal("linux", "windows");
	REQUIRE_FALSE(linuxFromWindows.empty());
	CHECK(linuxFromWindows.find("Linux") != Orkige::String::npos);
	CHECK(linuxFromWindows.find("Windows") != Orkige::String::npos);
	// a Windows host must never be told to "run the export on a Linux machine"
	// while being called Linux itself - the two names in the sentence differ
	CHECK(linuxFromWindows.find("runs on Windows") != Orkige::String::npos);

	// a Windows host is none of this function's business for the platforms
	// whose player is built elsewhere
	for(Orkige::String const & platform :
		{ "ios-simulator", "ios", "ios-ipa", "android", "android-aab", "web" })
	{
		CHECK(desktopHostRefusal(platform, "windows").empty());
	}

	// a host the exporter has no desktop target for at all still names the
	// platform and where to run the export, never a bare "unsupported"
	const Orkige::String fromNowhere = desktopHostRefusal("windows", "");
	REQUIRE_FALSE(fromNowhere.empty());
	CHECK(fromNowhere.find("Windows") != Orkige::String::npos);
}
