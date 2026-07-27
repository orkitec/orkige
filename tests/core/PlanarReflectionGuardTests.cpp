/**************************************************************
	created:	2026/07/27 at 10:00
	filename: 	PlanarReflectionGuardTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	The pure one-shot skip guard for the next backend's nested planar
	reflection update: byte-inert in the steady state (no rebuild => never
	skips), one skipped frame per window-workspace rebuild, and multiple
	rebuilds before an update coalesce to a single skipped frame. The rendered
	proof (mirror renders every steady-state frame, one stale frame across a
	scene switch) rides render_facade_selfcheck + water_mirror_wobble.
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include <core_util/PlanarReflectionGuard.h>

using namespace Orkige;

TEST_CASE("PlanarReflectionGuard: steady state never skips",
	"[planarguard]")
{
	PlanarReflectionGuard guard;
	CHECK_FALSE(guard.skipPending());
	// a long run with no rebuild: every frame's update proceeds (the mirror
	// renders every frame - the byte-inert contract)
	for(int frame = 0; frame < 100; ++frame)
	{
		CHECK_FALSE(guard.consumeSkip());
	}
}

TEST_CASE("PlanarReflectionGuard: a rebuild skips exactly one frame",
	"[planarguard]")
{
	PlanarReflectionGuard guard;
	guard.noteWorkspaceRebuilt();
	CHECK(guard.skipPending());
	// the frame after the rebuild is skipped (one frame of stale mirror)...
	CHECK(guard.consumeSkip());
	// ...and the mirror resumes the very next frame
	CHECK_FALSE(guard.skipPending());
	CHECK_FALSE(guard.consumeSkip());
	CHECK_FALSE(guard.consumeSkip());
}

TEST_CASE("PlanarReflectionGuard: repeated rebuilds coalesce to one skip",
	"[planarguard]")
{
	PlanarReflectionGuard guard;
	// several rebuilds land in one cycle before the next update (a scene switch
	// that flips shadow config AND refraction both rebuild the workspace) - the
	// cost is still a SINGLE skipped frame
	guard.noteWorkspaceRebuilt();
	guard.noteWorkspaceRebuilt();
	guard.noteWorkspaceRebuilt();
	CHECK(guard.consumeSkip());
	CHECK_FALSE(guard.consumeSkip());
}

TEST_CASE("PlanarReflectionGuard: the skip is consumed by the first update, "
	"not lost to an idle frame", "[planarguard]")
{
	PlanarReflectionGuard guard;
	// a rebuild happens, but the subsystem/camera is not yet live for a few
	// frames (updatePlanarReflections returns early before consuming). The
	// guard is only consumed once the update would actually run, so the one
	// skipped frame reliably lands on the first real mirror update.
	guard.noteWorkspaceRebuilt();
	// (the early-out frames simply never call consumeSkip)
	CHECK(guard.skipPending());
	// the first real update consumes it
	CHECK(guard.consumeSkip());
	CHECK_FALSE(guard.consumeSkip());
}
