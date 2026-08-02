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

using OrkigeEditor::EditorExportInputs;
using OrkigeEditor::EditorExportPlan;
using OrkigeEditor::EditorExportSource;
using OrkigeEditor::planProjectExport;

namespace
{
	//! an editor built in - and still living in - the source tree
	EditorExportInputs treeInputs()
	{
		EditorExportInputs inputs;
		inputs.platform = "macos";
		inputs.projectRoot = "/work/games/roller";
		inputs.engineTree = true;
		inputs.engineRoot = "/tree";
		inputs.engineBuildDir = "/tree/build/macos-debug";
		inputs.iosDeviceTree = "ios-device-debug";
		inputs.hostPlatform = "macos";
		inputs.hostName = "macOS";
		return inputs;
	}

	//! a copy of the app on a machine with no repository and no build tree:
	//! the engine payload rides inside it
	EditorExportInputs bundleInputs()
	{
		EditorExportInputs inputs = treeInputs();
		inputs.engineTree = false;
		// the paths CMake baked in are still there, naming directories that do
		// not exist on THIS machine - the plan must never reach for them
		inputs.bundleResources = "/Apps/Orkige.app/Contents/Resources/";
		inputs.bundleTools = "/Apps/Orkige.app/Contents/MacOS/";
		inputs.bundlePlayer = true;
		inputs.bundleMedia = true;
		return inputs;
	}

	//! does any sourcing field or the error text mention @p needle?
	bool mentions(EditorExportPlan const & plan, Orkige::String const & needle)
	{
		for(Orkige::String const & field : { plan.error, plan.engineBuild,
			plan.repoRoot, plan.bundleResources, plan.bundleTools,
			plan.enginePayload })
		{
			if(field.find(needle) != Orkige::String::npos)
			{
				return true;
			}
		}
		return false;
	}
}

TEST_CASE("export plan: a source-tree editor packages a preset build tree",
	"[editor][export]")
{
	const EditorExportPlan desktop = planProjectExport(treeInputs());
	REQUIRE(desktop.ok);
	CHECK(desktop.source == EditorExportSource::Tree);
	CHECK(desktop.platform == "macos");
	CHECK(desktop.projectRoot == "/work/games/roller");
	// the desktop app packages THIS editor's own tree
	CHECK(desktop.engineBuild == "/tree/build/macos-debug");
	CHECK(desktop.enginePayload == desktop.engineBuild);
	// ...with the source tree beside it supplying the engine media and the
	// module build scripts - the ONE "beside itself" input
	CHECK(desktop.repoRoot == "/tree");
	CHECK(desktop.bundleResources.empty());
	CHECK(desktop.bundleTools.empty());

	// every device target packages its own platform's preset tree - the
	// exporter reports honestly when one of those was never built
	EditorExportInputs inputs = treeInputs();
	inputs.platform = "ios-simulator";
	CHECK(planProjectExport(inputs).engineBuild ==
		"/tree/build/ios-simulator-debug");
	inputs.platform = "ios";
	CHECK(planProjectExport(inputs).engineBuild ==
		"/tree/build/ios-device-debug");
	inputs.platform = "android";
	CHECK(planProjectExport(inputs).engineBuild == "/tree/build/android-debug");
	inputs.platform = "web";
	CHECK(planProjectExport(inputs).engineBuild == "/tree/build/web-release");
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
	CHECK(plan.engineBuild == "/tree/build/macos-debug");
}

TEST_CASE("export plan: a copied app packages the payload it carries",
	"[editor][export]")
{
	const EditorExportPlan plan = planProjectExport(bundleInputs());
	REQUIRE(plan.ok);
	CHECK(plan.source == EditorExportSource::Bundle);
	CHECK(plan.platform == "macos");
	// the two roots the resource locator answered with - and NOTHING from the
	// machine the binary was built on
	CHECK(plan.bundleResources == "/Apps/Orkige.app/Contents/Resources/");
	CHECK(plan.bundleTools == "/Apps/Orkige.app/Contents/MacOS/");
	CHECK(plan.engineBuild.empty());
	CHECK(plan.enginePayload == "/Apps/Orkige.app/Contents/Resources/");
	CHECK_FALSE(mentions(plan, "/tree"));
}

TEST_CASE("export plan: a plan names exactly ONE engine source",
	"[editor][export]")
{
	// THE invariant: an export resolves files beside itself, so the engine
	// source is ONE field. A staged payload IS that source, which is why a
	// Bundle plan carries no repository root at all - there is nothing for the
	// exporter and the payload to disagree about (the other end of the same
	// seam refuses a request that fills both).
	for(Orkige::String const & platform : { Orkige::String("macos"),
		Orkige::String("web") })
	{
		EditorExportInputs inputs = bundleInputs();
		inputs.platform = platform;
		inputs.bundleWebPlayer = true;
		const EditorExportPlan plan = planProjectExport(inputs);
		REQUIRE(plan.ok);
		CHECK(plan.repoRoot.empty());
		CHECK(plan.engineBuild.empty());
	}
	// and the Tree shape is the mirror image: a repository, no staged roots
	const EditorExportPlan tree = planProjectExport(treeInputs());
	REQUIRE(tree.ok);
	CHECK_FALSE(tree.repoRoot.empty());
	CHECK(tree.bundleResources.empty());
	CHECK(tree.bundleTools.empty());
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

TEST_CASE("export plan: a copied app carrying the browser player packages it",
	"[editor][export]")
{
	// the browser is the ONE target a distributed copy can package for on any
	// host: a web build compiles nothing - the wasm player is a prebuilt
	// artifact staged inside the app and the rest is bytes the exporter
	// arranges - so the platform refusal must not fire for it.
	EditorExportInputs inputs = bundleInputs();
	inputs.platform = "web";
	inputs.bundleWebPlayer = true;
	const EditorExportPlan plan = planProjectExport(inputs);
	REQUIRE(plan.ok);
	CHECK(plan.source == EditorExportSource::Bundle);
	CHECK(plan.platform == "web");
	CHECK(plan.bundleResources == "/Apps/Orkige.app/Contents/Resources/");
	CHECK(plan.engineBuild.empty());
	CHECK_FALSE(mentions(plan, "/tree"));

	// ...on a host with no desktop packaging target of its own, too - the
	// browser build does not depend on one
	inputs.hostPlatform.clear();
	inputs.hostName = "Linux";
	CHECK(planProjectExport(inputs).ok);

	// but a copy WITHOUT the staged browser player still refuses, and the
	// device targets are unaffected by the browser player riding along
	inputs = bundleInputs();
	inputs.platform = "web";
	CHECK_FALSE(planProjectExport(inputs).ok);
	inputs.platform = "android";
	inputs.bundleWebPlayer = true;
	CHECK_FALSE(planProjectExport(inputs).ok);
}

TEST_CASE("export plan: an installed device player packages for a phone",
	"[editor][export]")
{
	// a phone's player is not carried but FETCHED, so a copied app packages
	// for that platform as soon as the download is installed - the caller
	// resolves the directory and hands it in, exactly like the SDK pack
	EditorExportInputs inputs = bundleInputs();
	inputs.platform = "ios-simulator";
	inputs.devicePayload = "/state/payloads/player-ios-simulator";
	const EditorExportPlan plan = planProjectExport(inputs);
	REQUIRE(plan.ok);
	CHECK(plan.source == EditorExportSource::Bundle);
	CHECK(plan.devicePayload == "/state/payloads/player-ios-simulator");
	// what a report shows: the pieces came out of the payload, not the app
	CHECK(plan.enginePayload == "/state/payloads/player-ios-simulator");
	// still no repository - the beside-itself invariant is unchanged
	CHECK(plan.repoRoot.empty());
	CHECK(plan.engineBuild.empty());

	// ...on a host with no desktop packaging target of its own, too: the
	// player was built elsewhere, so this host's own target is irrelevant
	inputs.hostPlatform.clear();
	inputs.hostName = "Linux";
	CHECK(planProjectExport(inputs).ok);

	// with NO payload the caller's ready-made sentence is what the person
	// reads - the refusal has one definition, beside the catalogue
	inputs = bundleInputs();
	inputs.platform = "ios-simulator";
	inputs.devicePayloadProblem = "the iOS Simulator player is not installed "
		"yet - fetch it under Settings > Build Targets";
	const EditorExportPlan refused = planProjectExport(inputs);
	CHECK_FALSE(refused.ok);
	CHECK(refused.error == inputs.devicePayloadProblem);
	CHECK_FALSE(mentions(refused, "/tree"));

	// a TREE plan never carries one: that shape takes the player out of the
	// platform's own preset build tree
	EditorExportInputs tree = treeInputs();
	tree.platform = "ios-simulator";
	tree.devicePayload = "/state/payloads/player-ios-simulator";
	const EditorExportPlan treePlan = planProjectExport(tree);
	REQUIRE(treePlan.ok);
	CHECK(treePlan.devicePayload.empty());
}

TEST_CASE("export plan: compiled game code needs an SDK, and says which",
	"[editor][export]")
{
	// a copy WITHOUT an installed SDK (or without the machine's build
	// toolchain) cannot build compiled game code, and the sentence that says
	// which of the two is missing comes from the ONE place that resolves them
	// (core_project/NativeModule.h) - the planner passes it through rather
	// than inventing a second wording.
	EditorExportInputs inputs = bundleInputs();
	inputs.nativeModule = true;
	inputs.nativeProblem = "the Orkige SDK for this build is not installed";
	const EditorExportPlan refused = planProjectExport(inputs);
	CHECK_FALSE(refused.ok);
	CHECK(refused.error == inputs.nativeProblem);

	// ...and with a pack installed the same copy packages the project: the
	// module is built against the pack, everything else is the payload it
	// already carries
	inputs.nativeProblem.clear();
	inputs.sdkPack = "/state/sdk/next";
	const EditorExportPlan plan = planProjectExport(inputs);
	REQUIRE(plan.ok);
	CHECK(plan.source == EditorExportSource::Bundle);
	CHECK(plan.sdkPack == "/state/sdk/next");
	CHECK(plan.repoRoot.empty());
	CHECK_FALSE(mentions(plan, "/tree"));

	// a Lua-only project never touches any of it
	EditorExportInputs scripted = bundleInputs();
	scripted.nativeProblem = "the Orkige SDK for this build is not installed";
	const EditorExportPlan scriptedPlan = planProjectExport(scripted);
	REQUIRE(scriptedPlan.ok);
	CHECK(scriptedPlan.sdkPack.empty());

	// the developer shape is unaffected: a build tree answers the same
	// question, so no pack travels with a Tree plan
	EditorExportInputs tree = treeInputs();
	tree.nativeModule = true;
	tree.sdkPack = "/state/sdk/next";
	const EditorExportPlan treePlan = planProjectExport(tree);
	REQUIRE(treePlan.ok);
	CHECK(treePlan.sdkPack.empty());
}

TEST_CASE("export plan: a payload-less copy says what it is missing",
	"[editor][export]")
{
	// a half-staged app: the app was never staged, or its engine payload did
	// not ride along
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
