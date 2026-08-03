/**************************************************************
	created:	2026/08/03 at 16:00
	filename: 	ScriptTestReport.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __ScriptTestReport_h__3_8_2026__16_00_00__
#define __ScriptTestReport_h__3_8_2026__16_00_00__

#include "core_module/OrkigePrerequisites.h"
#include "core_util/String.h"

namespace Orkige
{
	/** \addtogroup Script
	*  @{ */

	//! @brief one test's verdict. `status` is one of exactly three words:
	//!  - "pass"  - the body returned without raising
	//!  - "fail"  - an ASSERTION refused (t.eq / t.near / t.fail / ...)
	//!  - "error" - anything else raised, incl. the honest refusal of a test
	//!              that declares a not-yet-available option
	//! `message` is "" on a pass and otherwise carries the raised text, which
	//! Lua has already prefixed with "<project-relative file>:<line>: ".
	struct ScriptTestRecord
	{
		String	file;			//!< project-relative test file (the chunk name)
		String	name;			//!< the test's declared name ("" = a whole-file failure)
		String	status;			//!< "pass" / "fail" / "error"
		String	message;		//!< "" on a pass, the failure text otherwise
		double	ms = 0.0;		//!< wall time the body took
	};

	//! @brief one DECLARED test, as the declaration pass found it: what it is
	//! called and, when it declared one, the scene it wants to run in. A test
	//! naming a scene is a PLAY-MODE test - it needs a live world and frames,
	//! so the runner loads that scene and runs the body as a script task; a
	//! test with no scene needs neither and runs straight through.
	struct ScriptTestCase
	{
		int		index = 0;	//!< the declaration's index inside its file (the run key)
		String	name;		//!< the declared test name
		String	scene;		//!< "" for a test that needs no scene
	};

	//! @brief the run's closing tally (the artifact's last line)
	struct ScriptTestSummary
	{
		int		files = 0;		//!< test files discovered
		int		total = 0;		//!< tests RUN (declared minus filtered out)
		int		passed = 0;
		int		failed = 0;		//!< assertion failures
		int		errors = 0;		//!< raised errors + honest refusals
		int		filtered = 0;	//!< declared tests the filter excluded
		double	ms = 0.0;		//!< wall time of the whole run

		//! the ctest contract: 0 when nothing failed or errored
		int exitCode() const
		{
			return (this->failed == 0 && this->errors == 0) ? 0 : 1;
		}
	};

	//! @brief the JSONL RUN ARTIFACT - one JSON object per line, in the shape
	//! the breadcrumb trail and the benchmark results already use.
	//!
	//! WHY an artifact and not just an exit code: an agent loop needs to know
	//! WHICH test failed and WHY without scraping a log, and a run that CRASHES
	//! must still say which test was live. One line per record, flushed as it is
	//! produced, gives both - the last line in the file is the last thing that
	//! ran.
	//!
	//! Layout: one `meta` line (the run's identity), one `test` line per
	//! executed test, one `summary` line at the end. A file with no `summary`
	//! line is a run that died.
	//!
	//! Pure formatting: these functions build strings and perform no I/O, so
	//! the record shape is unit-testable and the file sink stays at the edge
	//! that owns a writable directory.
	namespace ScriptTestReport
	{
		//! @brief the opening `meta` line: what ran, where and with which
		//! filter. @p utc is an ISO 8601 timestamp ("" = omit).
		String metaLine(String const & project, String const & utc,
			String const & filter, int files);

		//! @brief one executed test's `test` line
		String testLine(ScriptTestRecord const & record);

		//! @brief the closing `summary` line
		String summaryLine(ScriptTestSummary const & summary);

		//! @brief the one-line human verdict the runtime logs (the same facts
		//! as the summary line, in prose): "12 passed, 1 failed, 0 errors".
		String summaryText(ScriptTestSummary const & summary);
	}

	/** @} */
}

#endif //__ScriptTestReport_h__3_8_2026__16_00_00__
