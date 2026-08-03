/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	PlayerTestRun.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __PlayerTestRun_h__3_8_2026__16_00_00__
#define __PlayerTestRun_h__3_8_2026__16_00_00__

#include <core_util/String.h>

#include <functional>

namespace Orkige
{
	class Project;
}

//! @brief what the runner needs from its HOST to run a play-mode test: a
//! world it can reset to a named scene, and a way to advance one frame.
//!
//! Both belong to whoever owns the frame loop, which is why they arrive as
//! callbacks: the tier's rules (isolation, ordering, the frame budget) live
//! in one place, and the player supplies the two things only it can do.
//! A runner given neither still runs every test that needs no scene and
//! refuses the play-mode ones honestly.
struct PlayerTestHooks
{
	//! @brief reset the world to this scene: a FULL teardown (every object,
	//! including any marked persistent - a test run is a boundary, and a
	//! survivor would couple one test to the next) followed by a fresh load.
	//! False when the scene could not be loaded.
	std::function<bool(Orkige::String const & scenePath)>	loadScene;
	//! @brief advance the host by exactly one frame. False when the run has
	//! ended (a quit, a closed window, a render failure) - the runner then
	//! stops and reports what it has.
	std::function<bool()>									pumpFrame;
};

//! @brief `--run-tests`: run the open project's own Lua test suite
//! (`<project>/tests/*.test.lua`) against the LIVE runtime and return the
//! process exit code (0 = everything passed) - the same exit-code contract
//! every player selfcheck ctest already uses.
//!
//! It runs where the runtime is fully up, so a test sees exactly the engine a
//! game sees: the resource mounts are live (so `script.require` reaches a
//! library out of a pak or an APK just as it does loose), the project's script
//! search root is set, and the sandbox is the hardened one.
//!
//! This TU owns the two EDGE concerns the pure core deliberately does not:
//! walking the project's `tests/` directory (there is nothing to enumerate
//! inside an archive, and test files never ship inside one) and appending the
//! JSONL run artifact record by record. Every DECISION - which files are
//! tests, which one wins a name clash, what the filter selects, what a record
//! looks like - lives in core_script and is unit-tested headlessly.
//!
//! The artifact lands in `ORKIGE_TEST_REPORT_DIR` when set (the isolation seam
//! a ctest uses), otherwise beside the breadcrumb trail in the writable app
//! dir, as `tests-<utcstamp>.jsonl`.
//!
//! TWO TIERS, ONE RUN and ONE player boot: the tests that need no scene run
//! FIRST (they are fast and cannot be disturbed by a world), then the
//! play-mode ones grouped by scene. Each play-mode test gets its own fresh
//! world and its own frame budget, so a wedged wait names itself instead of
//! hanging the run.
int runProjectLuaTests(Orkige::Project const & project,
	Orkige::String const & filter, Orkige::String const & fallbackReportDir,
	PlayerTestHooks const & hooks);

#endif //__PlayerTestRun_h__3_8_2026__16_00_00__
