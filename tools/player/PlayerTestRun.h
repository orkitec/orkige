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

namespace Orkige
{
	class Project;
}

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
int runProjectLuaTests(Orkige::Project const & project,
	Orkige::String const & filter, Orkige::String const & fallbackReportDir);

#endif //__PlayerTestRun_h__3_8_2026__16_00_00__
