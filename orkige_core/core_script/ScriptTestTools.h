/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	ScriptTestTools.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptTestTools_h__3_8_2026__16_00_00__
#define __ScriptTestTools_h__3_8_2026__16_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

#include <vector>

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */

	//! @brief one discovered Lua test FILE. `name` is the stable id (the file's
	//! base name with ".test.lua" stripped: tests/movement.test.lua ->
	//! "movement"); `resourceName` is the PROJECT-RELATIVE name the runtime
	//! loads it by ("tests/movement.test.lua"), which is also the chunk name
	//! every error message and `file:line` prefix is written in terms of.
	struct ScriptTestFile
	{
		String name;			//!< stable id (base name minus the suffix)
		String resourceName;	//!< project-relative path (the chunk name)
	};

	//! @brief the PURE discovery rules for the project TEST TIER: a project
	//! script whose file name ends in ".test.lua", living under
	//! `<project>/tests/`, is a test file. Suffix-marks-kind is the house
	//! convention already carried by `.component.lua` (a component KIND) and
	//! `.editor.lua` (an editor TOOL); this is the third member of that family
	//! and mirrors their discovery shape - the same recursive walk over a
	//! dedicated directory, the same "first of a duplicate name wins, both
	//! logged" rule.
	//!
	//! Everything here is PURE: the DECISION (which files are tests, which one
	//! wins a name clash, what order they run in) is separated from the
	//! directory WALK that produced the file list, so it is exhaustively
	//! unit-testable headlessly against a synthetic list and the core layer
	//! gains no filesystem access. The walk itself is a caller's business - a
	//! runtime enumerates loose files; there is nothing to enumerate inside an
	//! archive, and test files deliberately never ship inside one.
	namespace ScriptTestTools
	{
		//! the filename suffix that marks a project script as a test file
		char const * testFileSuffix();

		//! the project directory test files live in ("tests"), a sibling of
		//! `scripts/`. It is NOT a payload subdirectory: tests are development
		//! artefacts and stay out of every export by construction.
		char const * testsDirectoryName();

		//! @brief the stable test-file name for a file name/path: its base name
		//! with the ".test.lua" suffix stripped ("tests/a/movement.test.lua" ->
		//! "movement"); "" when the name is not a "*.test.lua" file (a plain
		//! library, a *.component.lua, or any other file).
		String testNameForFile(String const & fileName);

		//! @brief the discovery DECISION over an already-enumerated list of
		//! project-relative paths: keep the "*.test.lua" entries, order them by
		//! stable name (then path) so a run is reproducible, and drop a
		//! duplicate stable name keeping the FIRST in that order.
		//! @param outDuplicates (optional) receives one "<name>: kept <path>,
		//! ignored <path>" line per dropped file, so the caller can log the
		//! clash without this function knowing about a log.
		std::vector<ScriptTestFile> collectTestFiles(
			StringVector const & projectRelativePaths,
			StringVector * outDuplicates = 0);

		//! @brief does @p filter select the test identified by @p fileName /
		//! @p testName? An empty filter selects everything; otherwise it is a
		//! plain SUBSTRING matched against "<file>::<test name>", so
		//! `--test-filter movement` selects a whole file and
		//! `--test-filter "is symmetric"` selects one case across files.
		bool filterMatches(String const & filter, String const & fileName,
			String const & testName);
	}

	/** @} */
}

#endif //__ScriptTestTools_h__3_8_2026__16_00_00__
