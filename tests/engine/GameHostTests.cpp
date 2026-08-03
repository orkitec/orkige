/********************************************************************
	created:	Saturday 2026/08/02 at 09:00
	filename: 	GameHostTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// Game host tests - the pure, headless corners of the reusable game host
// (engine_runtime/GameHost.h): the frame-loop driver's ownership and call
// contract, the canonical tick order's deferred-load pump, the build
// identity and the platform harness's declared defaults. The packaging
// prologue itself (an APK, a browser payload) is covered by the device and
// web ctests, which are the only places those archives exist.
#include <catch2/catch_test_macros.hpp>

#include <engine_runtime/GameHost.h>
#include <core_game/LevelManager.h>

#include <string>
#include <vector>

namespace
{
	//! a stand-in run state: the driver only ever sees a void*
	struct FakeRun
	{
		int				framesLeft = 0;
		int				framesRun = 0;
		int				finishCount = 0;
		int				disposeCount = 0;
		int				exitCode = 7;
	};

	//! the four callbacks a host hands the driver, as capture-less lambdas
	Orkige::GameFrameLoop makeLoop(FakeRun & run)
	{
		Orkige::GameFrameLoop loop;
		loop.context = &run;
		loop.frame = [](void* raw)
		{
			FakeRun& state = *static_cast<FakeRun*>(raw);
			++state.framesRun;
			--state.framesLeft;
			return state.framesLeft > 0;
		};
		loop.finish = [](void* raw)
		{
			++static_cast<FakeRun*>(raw)->finishCount;
		};
		loop.exitCode = [](void* raw)
		{
			return static_cast<FakeRun*>(raw)->exitCode;
		};
		loop.dispose = [](void* raw)
		{
			++static_cast<FakeRun*>(raw)->disposeCount;
		};
		return loop;
	}
}

TEST_CASE("the frame loop runs every frame and finishes exactly once",
	"[gamehost]")
{
	if (Orkige::gameFrameLoopOwnsContext())
	{
		// a platform whose loop never returns cannot be driven from a test
		// process: it would take this run state and exit the runtime
		SUCCEED("the loop owns the context on this platform");
		return;
	}
	FakeRun run;
	run.framesLeft = 4;
	Orkige::runGameFrameLoop(makeLoop(run));
	// the body ran until it said the run had ended - four frames, then the
	// ONE orderly shutdown
	CHECK(run.framesRun == 4);
	CHECK(run.finishCount == 1);
	// where the loop RETURNS the caller still owns its run state: destroying
	// it here would pull the world out from under main()'s teardown
	CHECK(run.disposeCount == 0);
}

TEST_CASE("the frame loop always runs at least one frame", "[gamehost]")
{
	if (Orkige::gameFrameLoopOwnsContext())
	{
		SUCCEED("the loop owns the context on this platform");
		return;
	}
	// the body decides when the run is over, not the driver: a first frame
	// that reports "ended" still HAPPENED (it is the frame that booted the
	// world and hit a frame cap of one)
	FakeRun run;
	run.framesLeft = 1;
	Orkige::runGameFrameLoop(makeLoop(run));
	CHECK(run.framesRun == 1);
	CHECK(run.finishCount == 1);
}

TEST_CASE("the deferred-load pump hands the pending scene to the host",
	"[gamehost]")
{
	Orkige::LevelManager levels;
	levels.loadScenePath("scenes/next.oscene");
	std::vector<std::string> requested;
	Orkige::GameTick tick;
	tick.levels = &levels;
	tick.loadScene = [&requested](Orkige::String const & scene)
	{
		requested.push_back(scene);
		return true;
	};
	// every other subsystem is absent - a host that owns none of them still
	// gets a working frame
	Orkige::advanceGameWorld(tick, 1.0f / 60.0f);
	REQUIRE(requested.size() == 1);
	CHECK(requested.front() == "scenes/next.oscene");
	// the request is consumed: the next frame asks for nothing
	Orkige::advanceGameWorld(tick, 1.0f / 60.0f);
	CHECK(requested.size() == 1);
}

TEST_CASE("a refused deferred load leaves the current level alone",
	"[gamehost]")
{
	Orkige::LevelManager levels;
	levels.setCurrentIndex(2);
	levels.loadScenePath("scenes/broken.oscene");
	Orkige::GameTick tick;
	tick.levels = &levels;
	tick.loadScene = [](Orkige::String const &) { return false; };
	Orkige::advanceGameWorld(tick, 1.0f / 60.0f);
	// loadScenePath carries no level index, so the index never moves either
	// way; what matters is that a failed load is not treated as an arrival
	CHECK(levels.currentIndex() == 2);
}

TEST_CASE("an empty tick advances nothing and does not crash", "[gamehost]")
{
	// the shape a host boots with before it owns any subsystem
	const Orkige::GameTick tick;
	Orkige::advanceGameWorld(tick, 1.0f / 60.0f);
	SUCCEED("an all-absent tick is a no-op");
}

TEST_CASE("describeBuild names this binary in full", "[gamehost]")
{
	const Orkige::GameBuildIdentity identity = Orkige::describeBuild();
	CHECK_FALSE(identity.flavor.empty());
	CHECK_FALSE(identity.platform.empty());
	CHECK_FALSE(identity.renderSystem.empty());
	CHECK_FALSE(identity.build.empty());
	// the flavor is compiled in, never guessed
#ifdef ORKIGE_RENDER_NEXT
	CHECK(identity.flavor == "next");
#else
	CHECK(identity.flavor == "classic");
#endif
#ifdef NDEBUG
	CHECK(identity.build == "Release");
#else
	CHECK(identity.build == "Debug");
#endif
}

TEST_CASE("GamePlatformConfig defaults describe a nameless desktop host",
	"[gamehost]")
{
	const Orkige::GamePlatformConfig config;
	CHECK_FALSE(config.appName.empty());
	CHECK_FALSE(config.logTag.empty());
	// a host that names no media reads whatever the engine baked in
	CHECK(config.desktopMediaDirectory.empty());
	CHECK(config.desktopContentDirectories.empty());
	CHECK(config.bundleContentSubdirectories.empty());
	CHECK_FALSE(config.bundledSceneName.empty());
}

TEST_CASE("the platform harness resolves its writable paths", "[gamehost]")
{
	Orkige::GamePlatform platform;
	platform.resolveDirectories("host_test.log", false);
	// the state directory is separator-terminated, so a caller appends a file
	// name to it directly
	const Orkige::String state = platform.getStateDirectory();
	REQUIRE_FALSE(state.empty());
	CHECK(state.back() == '/');
	CHECK_FALSE(platform.getEngineLogPath().empty());
	if (Orkige::GamePlatform::isMobile())
	{
		// a sandboxed app has no writable cwd: the log lives in its container
		CHECK(platform.getEngineLogPath() == state + "host_test.log");
	}
	else
	{
		// a dev run keeps its log in the cwd; an app that boots a project it
		// BUNDLES writes into the app-support directory instead
		CHECK(platform.getEngineLogPath() == "host_test.log");
		Orkige::GamePlatform bundled;
		bundled.resolveDirectories("host_test.log", true);
		CHECK(bundled.getEngineLogPath() ==
			bundled.getStateDirectory() + "host_test.log");
	}
}

TEST_CASE("the platform scene rule matches the platform's packaging",
	"[gamehost]")
{
	Orkige::GamePlatform platform;
	if (Orkige::GamePlatform::isMobile())
	{
		// a packaged app launched with nothing still has a scene to show
		CHECK_FALSE(platform.resolveScenePath("").empty());
	}
	else
	{
		// a desktop run keeps an empty path empty, so the host can print its
		// usage line instead of guessing
		CHECK(platform.resolveScenePath("").empty());
		CHECK(platform.resolveScenePath("scenes/level.oscene") ==
			"scenes/level.oscene");
	}
}
