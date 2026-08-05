/********************************************************************
	created:	Wednesday 2026/08/05 at 15:00
	filename: 	ExportLinuxTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
/*
	The decisions behind the Linux package: what the artifact is called, and
	which host may produce one at all.

	The second is the load-bearing half. A desktop package is assembled AROUND
	A BINARY and nothing here cross-compiles a player, so an export asked for
	the other desktop system must refuse BY NAME - the alternative is a
	directory that looks complete, installs anywhere, and cannot run. That
	refusal is a pure function precisely so it can be asserted on a host that
	is not the one being refused.
*/
#include <catch2/catch_test_macros.hpp>

#include "ExportLinux.h"
#include "ExportRun.h"
#include "ExportSettings.h"

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

TEST_CASE("linux is a platform the exporter packages", "[unit][export]")
{
	CHECK(isPackagedPlatform("linux"));
	CHECK(isDesktopPlatform("linux"));
	CHECK(isDesktopPlatform("macos"));
	// the platforms whose artifact is not assembled around the host's own
	// binary: each ships a player built for somewhere else
	CHECK_FALSE(isDesktopPlatform("ios-simulator"));
	CHECK_FALSE(isDesktopPlatform("ios"));
	CHECK_FALSE(isDesktopPlatform("android"));
	CHECK_FALSE(isDesktopPlatform("web"));
	CHECK_FALSE(isDesktopPlatform(""));
	// the texture cook reads the sidecar's DEFAULT block for a desktop, the
	// same one macOS gets - a desktop GPU wants no mobile container
	CHECK(cookPlatformToken("linux") == "");
	CHECK(cookPlatformToken("linux") == cookPlatformToken("macos"));
}

TEST_CASE("the linux artifact is one name, usable on a command line",
	"[unit][export]")
{
	// the directory a person cds into and the binary inside it carry the SAME
	// name, and it is the executable name rather than the display one: a
	// display name may hold spaces, and a game reached by typing its path
	// should not need quoting
	CHECK(linuxAppDirectoryName(projectNamed("My Game 2")) == "MyGame2");
	CHECK(linuxAppDirectoryName(projectNamed("My Game 2")) ==
		projectNamed("My Game 2").exeName());
	// a name with nothing to keep still yields a runnable directory
	CHECK(linuxAppDirectoryName(projectNamed("---")) == "OrkigeGame");
}

TEST_CASE("a desktop package is refused off its own host", "[unit][export]")
{
	// the host that CAN package says nothing - a refusal is what is asserted,
	// so an empty answer is the permission
	CHECK(desktopHostRefusal("linux", "linux").empty());
	CHECK(desktopHostRefusal("macos", "macos").empty());
	// ...and every platform whose player is built elsewhere is none of this
	// function's business, whichever host asks
	for(Orkige::String const & platform :
		{ "ios-simulator", "ios", "ios-ipa", "android", "android-aab", "web" })
	{
		CHECK(desktopHostRefusal(platform, "macos").empty());
		CHECK(desktopHostRefusal(platform, "linux").empty());
		CHECK(desktopHostRefusal(platform, "").empty());
	}

	// the cross case: the refusal names the platform asked for, the host that
	// was asked, and the reason - a person reading it must learn what to do
	const Orkige::String fromMac = desktopHostRefusal("linux", "macos");
	REQUIRE_FALSE(fromMac.empty());
	CHECK(fromMac.find("Linux") != Orkige::String::npos);
	CHECK(fromMac.find("macOS") != Orkige::String::npos);
	CHECK(fromMac.find("cross-compile") != Orkige::String::npos);

	const Orkige::String fromLinux = desktopHostRefusal("macos", "linux");
	REQUIRE_FALSE(fromLinux.empty());
	CHECK(fromLinux.find("macOS") != Orkige::String::npos);
	CHECK(fromLinux.find("Linux") != Orkige::String::npos);

	// a host the exporter has no desktop target for at all still names the
	// platform and where to run the export, never a bare "unsupported"
	const Orkige::String fromNowhere = desktopHostRefusal("linux", "");
	REQUIRE_FALSE(fromNowhere.empty());
	CHECK(fromNowhere.find("Linux") != Orkige::String::npos);
}

TEST_CASE("this build names the desktop platform it can package",
	"[unit][export]")
{
	// a compile-time fact, so it is asserted against the compiler's own idea
	// of the host rather than against a probe
	const Orkige::String host = hostDesktopPlatform();
#if defined(__APPLE__)
	CHECK(host == "macos");
#elif defined(__linux__)
	CHECK(host == "linux");
#else
	CHECK(host.empty());
#endif
	// whatever it answered, it is a platform the exporter packages - and one
	// this host is allowed to package
	if(!host.empty())
	{
		CHECK(isPackagedPlatform(host));
		CHECK(isDesktopPlatform(host));
		CHECK(desktopHostRefusal(host, host).empty());
	}
}
