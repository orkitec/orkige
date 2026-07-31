/**************************************************************
	created:	2026/07/31 at 09:00
	filename: 	EditorExportPlanTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "EditorExportPlan.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using OrkigeEditor::EditorExportInputs;
using OrkigeEditor::EditorExportPlan;
using OrkigeEditor::EditorExportSource;
using OrkigeEditor::EditorResourcePath;
using OrkigeEditor::EditorResourceRoot;
using OrkigeEditor::planProjectExport;

namespace
{
	//! an editor built in - and still living in - the source tree
	EditorExportInputs treeInputs()
	{
		EditorExportInputs inputs;
		inputs.platform = "macos";
		inputs.projectRoot = "/work/games/roller";
		inputs.exporter = EditorResourcePath{"/tree/Util/orkige_export.py",
			EditorResourceRoot::Tree};
		inputs.engineTree = true;
		inputs.engineRoot = "/tree";
		inputs.engineBuildDir = "/tree/build/macos-debug";
		inputs.iosDeviceTree = "ios-device-debug";
		inputs.hostPlatform = "macos";
		inputs.hostName = "macOS";
		return inputs;
	}

	//! a copy of the app on a machine with no repository and no build tree:
	//! the exporter and the engine payload both ride inside it
	EditorExportInputs bundleInputs()
	{
		EditorExportInputs inputs = treeInputs();
		inputs.exporter = EditorResourcePath{
			"/Apps/Orkige.app/Contents/Resources/Util/orkige_export.py",
			EditorResourceRoot::Bundle};
		inputs.engineTree = false;
		// the paths CMake baked in are still there, naming directories that do
		// not exist on THIS machine - the plan must never reach for them
		inputs.bundleResources = "/Apps/Orkige.app/Contents/Resources/";
		inputs.bundleTools = "/Apps/Orkige.app/Contents/MacOS/";
		inputs.bundlePlayer = true;
		inputs.bundleMedia = true;
		return inputs;
	}

	//! the argument following @p flag, or "" when the plan carries no such flag
	Orkige::String valueOf(EditorExportPlan const & plan,
		Orkige::String const & flag)
	{
		for(size_t i = 0; i + 1 < plan.arguments.size(); ++i)
		{
			if(plan.arguments[i] == flag)
			{
				return plan.arguments[i + 1];
			}
		}
		return Orkige::String();
	}

	//! does any argument or the error text mention @p needle?
	bool mentions(EditorExportPlan const & plan, Orkige::String const & needle)
	{
		if(plan.error.find(needle) != Orkige::String::npos)
		{
			return true;
		}
		return std::any_of(plan.arguments.begin(), plan.arguments.end(),
			[&needle](Orkige::String const & argument)
			{
				return argument.find(needle) != Orkige::String::npos;
			});
	}
}

TEST_CASE("export plan: a source-tree editor packages a preset build tree",
	"[editor][export]")
{
	const EditorExportPlan desktop = planProjectExport(treeInputs());
	REQUIRE(desktop.ok);
	CHECK(desktop.source == EditorExportSource::Tree);
	CHECK(desktop.arguments.front() == "/tree/Util/orkige_export.py");
	CHECK(valueOf(desktop, "--project") == "/work/games/roller");
	CHECK(valueOf(desktop, "--platform") == "macos");
	// the desktop app packages THIS editor's own tree
	CHECK(valueOf(desktop, "--engine-build") == "/tree/build/macos-debug");
	CHECK(desktop.engineBuild == "/tree/build/macos-debug");
	CHECK(desktop.enginePayload == desktop.engineBuild);

	// every device target packages its own platform's preset tree - the
	// exporter reports honestly when one of those was never built
	EditorExportInputs inputs = treeInputs();
	inputs.platform = "ios-simulator";
	CHECK(valueOf(planProjectExport(inputs), "--engine-build") ==
		"/tree/build/ios-simulator-debug");
	inputs.platform = "ios";
	CHECK(valueOf(planProjectExport(inputs), "--engine-build") ==
		"/tree/build/ios-device-debug");
	inputs.platform = "android";
	CHECK(valueOf(planProjectExport(inputs), "--engine-build") ==
		"/tree/build/android-debug");
	inputs.platform = "web";
	CHECK(valueOf(planProjectExport(inputs), "--engine-build") ==
		"/tree/build/web-release");
}

TEST_CASE("export plan: a source-tree editor builds a native module too",
	"[editor][export]")
{
	// compiled C++ game code is the tree's business and stays supported there:
	// the exporter builds the module against the engine tree it was given
	EditorExportInputs inputs = treeInputs();
	inputs.nativeModule = true;
	const EditorExportPlan plan = planProjectExport(inputs);
	REQUIRE(plan.ok);
	CHECK(valueOf(plan, "--engine-build") == "/tree/build/macos-debug");
}

TEST_CASE("export plan: a copied app packages the payload it carries",
	"[editor][export]")
{
	const EditorExportPlan plan = planProjectExport(bundleInputs());
	REQUIRE(plan.ok);
	CHECK(plan.source == EditorExportSource::Bundle);
	CHECK(plan.arguments.front() ==
		"/Apps/Orkige.app/Contents/Resources/Util/orkige_export.py");
	CHECK(valueOf(plan, "--platform") == "macos");
	// the two roots the resource locator answered with - and NOTHING from the
	// machine the binary was built on
	CHECK(valueOf(plan, "--engine-bundle") ==
		"/Apps/Orkige.app/Contents/Resources/");
	CHECK(valueOf(plan, "--engine-tools") == "/Apps/Orkige.app/Contents/MacOS/");
	CHECK(plan.engineBuild.empty());
	CHECK(plan.enginePayload == "/Apps/Orkige.app/Contents/Resources/");
	CHECK_FALSE(mentions(plan, "/tree"));
	CHECK_FALSE(mentions(plan, "--engine-build"));
}

TEST_CASE("export plan: a copied app names the platform it cannot build",
	"[editor][export]")
{
	// a mobile or browser package needs that platform's player, which only a
	// source build produces. The refusal has to SAY that - a person reading it
	// must learn what is missing and what to do instead.
	for(Orkige::String const & platform : { Orkige::String("ios-simulator"),
		Orkige::String("ios"), Orkige::String("android"),
		Orkige::String("web") })
	{
		EditorExportInputs inputs = bundleInputs();
		inputs.platform = platform;
		const EditorExportPlan plan = planProjectExport(inputs);
		CHECK_FALSE(plan.ok);
		CHECK(plan.arguments.empty());
		CHECK(plan.error.find("player") != Orkige::String::npos);
		CHECK(plan.error.find("build Orkige from source") !=
			Orkige::String::npos);
		// the message names the platform in words, never a build-machine path
		CHECK_FALSE(mentions(plan, "/tree"));
	}
	// ...and each names ITS platform, not a generic "not supported"
	EditorExportInputs android = bundleInputs();
	android.platform = "android";
	CHECK(planProjectExport(android).error.find("Android") !=
		Orkige::String::npos);
	EditorExportInputs ios = bundleInputs();
	ios.platform = "ios";
	CHECK(planProjectExport(ios).error.find("iOS") != Orkige::String::npos);
}

TEST_CASE("export plan: a copied app cannot build compiled game code",
	"[editor][export]")
{
	EditorExportInputs inputs = bundleInputs();
	inputs.nativeModule = true;
	const EditorExportPlan plan = planProjectExport(inputs);
	CHECK_FALSE(plan.ok);
	CHECK(plan.error.find("native.target") != Orkige::String::npos);
	CHECK(plan.error.find("toolchain") != Orkige::String::npos);
}

TEST_CASE("export plan: no exporter anywhere refuses instead of guessing",
	"[editor][export]")
{
	// the failure this whole seam exists to prevent: with no staged exporter
	// and no reachable tree there is nothing to run, and inventing a path would
	// only fail later naming a directory from the machine that built the binary
	EditorExportInputs inputs = bundleInputs();
	inputs.exporter = EditorResourcePath{};
	const EditorExportPlan plan = planProjectExport(inputs);
	CHECK_FALSE(plan.ok);
	CHECK(plan.arguments.empty());
	CHECK(plan.error.find("orkige_export.py") != Orkige::String::npos);
	CHECK(plan.error.find("reinstall") != Orkige::String::npos);
}

TEST_CASE("export plan: a payload-less copy says what it is missing",
	"[editor][export]")
{
	// a half-staged app: the exporter script rode along but the engine payload
	// did not (or the app was never staged at all)
	EditorExportInputs inputs = bundleInputs();
	inputs.bundlePlayer = false;
	CHECK_FALSE(planProjectExport(inputs).ok);
	CHECK(planProjectExport(inputs).error.find("engine payload") !=
		Orkige::String::npos);

	inputs = bundleInputs();
	inputs.bundleMedia = false;
	CHECK_FALSE(planProjectExport(inputs).ok);

	inputs = bundleInputs();
	inputs.bundleResources.clear();
	CHECK_FALSE(planProjectExport(inputs).ok);
}

TEST_CASE("export plan: a host with no packaging target says so",
	"[editor][export]")
{
	// the exporter writes a macOS app today; a copy running elsewhere refuses
	// with that fact rather than starting an export that produces nothing
	EditorExportInputs inputs = bundleInputs();
	inputs.hostPlatform.clear();
	inputs.hostName = "Linux";
	const EditorExportPlan plan = planProjectExport(inputs);
	CHECK_FALSE(plan.ok);
	CHECK(plan.error.find("Linux") != Orkige::String::npos);
	CHECK(plan.error.find("macOS") != Orkige::String::npos);
}

TEST_CASE("export plan: an unknown platform is refused before anything else",
	"[editor][export]")
{
	EditorExportInputs inputs = treeInputs();
	inputs.platform = "playstation";
	const EditorExportPlan plan = planProjectExport(inputs);
	CHECK_FALSE(plan.ok);
	CHECK(plan.error.find("playstation") != Orkige::String::npos);
	CHECK_FALSE(OrkigeEditor::isExportPlatform("playstation"));
	CHECK(OrkigeEditor::isExportPlatform("macos"));
	CHECK(OrkigeEditor::isExportPlatform("web"));
}
