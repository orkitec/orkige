/********************************************************************
	created:	Tuesday 2026/08/04 at 12:00
	filename: 	EditorProjectTests.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
#ifndef __EditorProjectTests_h__4_8_2026__12_00_00__
#define __EditorProjectTests_h__4_8_2026__12_00_00__

#include <core_script/ScriptTestReport.h>
#include <core_script/ScriptTestTools.h>
#include <core_util/String.h>

#include <cstddef>
#include <vector>

//! @file EditorProjectTests.h
//! @brief the PURE half of the editor's Tests surface: what a run artifact
//! accumulates into, and the decisions a run is made of.
//!
//! @par The runner is the player's, and its artifact IS the result model
//! A project tests its own Lua in Lua, and the thing that runs a suite is the
//! player (`--run-tests`): a test that declares a scene needs a live world,
//! and the editor has none in edit mode - it never ticks game objects, so
//! `ScriptComponent` is dormant there by design. So the editor does not run a
//! test. It starts the runner and reads back the runner's own JSONL artifact
//! (@see core_script/ScriptTestReport.h), whose records are reused here
//! wholesale: @ref Orkige::ScriptTestRecord and @ref Orkige::ScriptTestSummary
//! are the model, not a copy of it, and the parse is the writer's own inverse.
//!
//! @par Why the artifact and not the exit code
//! An exit code says "something failed". A panel has to say WHICH test failed,
//! with the message and the place. The artifact carries one line per record,
//! FLUSHED as it is produced - which is what makes a run readable WHILE it
//! runs, and what makes a run that DIED still name the test that was live: a
//! report with no `summary` line did not finish, and its last `test` line is
//! where it was.
//!
//! @par One filter, so several legs
//! The runner takes ONE filter - a plain substring matched against
//! `<file>::<test name>` (@see ScriptTestTools::filterMatches). An arbitrary
//! SET of tests is not expressible as one substring, so a run is a LIST of
//! legs, each its own runner invocation, and the records accumulate across
//! them. Running everything is one leg with an empty filter; re-running
//! failures is one leg per failure, minus the ones a broader leg already
//! covers. The filter grammar is not extended and the runner is not touched.

namespace OrkigeEditor
{
	//! @brief the identity of the run a report came from (the artifact's
	//! opening `meta` line).
	typedef Orkige::ScriptTestReport::MetaRecord ProjectTestMeta;

	//! @brief the FILTER SUBJECT a record is identified by: `<file>::<name>`.
	//! It is both the record's identity when results merge and, verbatim, a
	//! filter that selects that test again.
	Orkige::String projectTestKey(Orkige::ScriptTestRecord const & record);

	//! @brief did this record pass? (the artifact's closed status vocabulary:
	//! anything that is not "pass" is a failure a reader must show)
	bool projectTestPassed(Orkige::ScriptTestRecord const & record);

	//! @brief where a failure points, decoded from the message text.
	//!
	//! Lua prefixes a raised message with `<chunk name>:<line>: `, and the
	//! runner loads a test file under its PROJECT-RELATIVE name - so the
	//! prefix already IS a project-relative `file:line` and needs no path
	//! guessing on the editor's side.
	struct ProjectTestLocation
	{
		Orkige::String	file;		//!< project-relative, "" when there is none
		int				line = 0;	//!< 1-based, 0 when there is none

		bool found() const { return !this->file.empty() && this->line > 0; }
	};

	//! @brief the `file:line` a failure message opens with; empty when it
	//! carries none - a refusal the RUNNER authored ("the scene could not be
	//! loaded") has no source position, and inventing one would send a reader
	//! to the wrong place.
	ProjectTestLocation projectTestFailureLocation(
		Orkige::String const & message);

	//! @brief what a run produced: the records, plus the two boundary lines
	//! that say whether the artifact is complete.
	//!
	//! @remarks Fed line by line as the artifact grows, so it is equally the
	//! live view of a run in flight and the finished result of one.
	struct ProjectTestReport
	{
		ProjectTestMeta								meta;
		bool										hasMeta = false;
		std::vector<Orkige::ScriptTestRecord>		records;
		Orkige::ScriptTestSummary					summary;
		//! false both while a run is in flight and after one that DIED - the
		//! caller tells those apart by whether the process is still alive
		bool										hasSummary = false;

		//! @brief the records that did not pass, in the order they ran
		std::vector<Orkige::ScriptTestRecord> failures() const;
		//! @brief the tally the RECORDS add up to.
		//! @remarks Not the `summary` line: a multi-leg run writes one summary
		//! per leg, so no single one of them describes the whole run. The
		//! records are the only thing that spans it.
		Orkige::ScriptTestSummary tally() const;
	};

	//! @brief feed one artifact line into a report; returns which kind it was.
	//! A `test` line REPLACES the record with the same key - so re-running one
	//! test updates its row in place instead of appending a second verdict -
	//! and otherwise appends.
	Orkige::ScriptTestReport::LineKind feedProjectTestLine(
		ProjectTestReport & report, Orkige::String const & line);

	//! @brief consume every COMPLETE line at the front of @p buffer into
	//! @p report and leave the trailing partial line in the buffer.
	//!
	//! This is the tail-a-growing-file entry point, and the partial-line rule
	//! is the whole point of it: the writer flushes per record but a reader
	//! can still catch a line mid-write, and a half-parsed record would be a
	//! fabricated verdict.
	//! @return how many complete lines were consumed.
	std::size_t consumeProjectTestLines(Orkige::String & buffer,
		ProjectTestReport & report);

	//! @brief one run: the legs to drive, in order, and what to call it.
	struct ProjectTestRunPlan
	{
		//! one runner invocation each; "" means no filter (everything)
		std::vector<Orkige::String>	filters;
		//! the one-line human description of what this run covers
		Orkige::String				label;

		bool empty() const { return this->filters.empty(); }
	};

	//! @brief does this plan run the WHOLE suite? True exactly when some leg
	//! carries no filter, since an empty filter selects everything.
	//!
	//! @remarks It is the rule for whether a run REPLACES the previous
	//! results or updates its own rows in them. A run that covers everything
	//! may clear what came before; a filtered one may not, or re-running one
	//! failure would erase the passes that are still true and leave a person
	//! staring at a suite that appears to be nothing but its failures.
	bool projectTestPlanCoversAll(ProjectTestRunPlan const & plan);

	//! the whole suite: one leg, no filter
	ProjectTestRunPlan planAllProjectTests();
	//! every test of one file (its project-relative name IS the filter)
	ProjectTestRunPlan planProjectTestFile(Orkige::String const & file);
	//! exactly one declared test
	ProjectTestRunPlan planProjectTest(Orkige::String const & file,
		Orkige::String const & name);
	//! @brief the failures of a previous report, one leg each.
	ProjectTestRunPlan planRerunFailedProjectTests(
		ProjectTestReport const & previous);

	//! @brief drop every filter that another filter in the list already
	//! covers, and collapse duplicates, preserving first-seen order.
	//!
	//! @remarks The rule follows from the grammar, not from taste: the filter
	//! is a SUBSTRING match, so a leg whose filter is a substring of another
	//! leg's already selects everything that other leg would. Dropping the
	//! longer one is therefore lossless - and it is what keeps two failures
	//! whose names share a prefix from running the same test twice. An empty
	//! filter is a substring of everything, so its presence collapses the plan
	//! to a single leg.
	std::vector<Orkige::String> reduceProjectTestFilters(
		std::vector<Orkige::String> const & filters);

	//! where a run is in its life
	enum class ProjectTestRunState
	{
		Idle,		//!< nothing has been asked for
		Running,	//!< a leg is in flight
		Cancelled,	//!< stopped on request; what was learned so far stands
		Finished	//!< every leg ran to an exit code
	};

	//! @brief the PURE lifecycle of a multi-leg run.
	//!
	//! It owns no process and reads no clock: the driver starts a leg for
	//! @ref currentFilter and reports that leg's exit code back. Ordering, the
	//! stop condition and the verdict are therefore testable headlessly, and
	//! the driver is left with nothing to decide.
	class ProjectTestRunProgress
	{
		//--- Methods -----------------------------------------
	public:
		//! @brief arm a plan. An EMPTY plan lands straight in @ref
		//! ProjectTestRunState::Finished: there was nothing to run, and
		//! reporting Running would leave a caller polling for a leg that never
		//! starts.
		void begin(ProjectTestRunPlan const & plan);
		//! @brief the filter of the leg in flight. "" means both "run
		//! everything" and "no leg is in flight", so read @ref state first.
		Orkige::String const & currentFilter() const;
		//! @brief the leg in flight exited with @p exitCode: fold it into the
		//! verdict and advance to the next leg, or finish. Ignored unless a
		//! leg is actually in flight.
		void legFinished(int exitCode);
		//! @brief stop after the leg in flight. Nothing already recorded is
		//! discarded: a cancelled run reports what it managed to learn, which
		//! is the whole reason a person cancels one.
		void cancel();

		ProjectTestRunState state() const { return this->mState; }
		bool running() const
		{
			return this->mState == ProjectTestRunState::Running;
		}
		//! 1-based index of the leg in flight (0 when none is)
		std::size_t leg() const;
		std::size_t legCount() const { return this->mPlan.filters.size(); }
		Orkige::String const & label() const { return this->mPlan.label; }
		//! @brief the worst exit code any leg reported: 0 only when every leg
		//! that ran reported 0.
		int exitCode() const { return this->mExitCode; }
		//--- Variables ---------------------------------------
	private:
		ProjectTestRunPlan	mPlan;
		std::size_t			mLeg = 0;	//!< index of the leg in flight
		ProjectTestRunState	mState = ProjectTestRunState::Idle;
		int					mExitCode = 0;
	};

	//! @brief the facts that decide whether a project's suite can be run at
	//! all, gathered by whichever door is asking.
	struct ProjectTestPreflight
	{
		Orkige::String	projectName;		//!< for the sentence
		Orkige::String	testsDirectory;		//!< for the sentence
		bool			projectOpen = false;
		bool			scriptingAvailable = false;	//!< this BUILD has a backend
		bool			testsDirectoryExists = false;
		bool			playerFound = false;
		bool			runInFlight = false;	//!< only the editor session can
		bool			planEmpty = false;		//!< nothing was selected
	};

	//! @brief why this run cannot start, as ONE actionable sentence; "" when
	//! it can.
	//!
	//! @remarks There are two doors onto the runner inside the editor binary -
	//! the headless `test` subcommand and the Tests panel / MCP session - and
	//! they run in different phases of the process, so they cannot share a
	//! code path. They must not therefore hold two OPINIONS about whether a
	//! project is testable: this is that one opinion, pure and unit-tested, so
	//! a refusal reads the same wherever it was asked for.
	Orkige::String projectTestRunRefusal(ProjectTestPreflight const & facts);

	//! @brief (re)scan `<projectRoot>/tests/` for the project's test FILES.
	//!
	//! @remarks FILES, not tests. A test only exists once its file's chunk has
	//! RUN - the declaration pass is Lua executing - and the editor runs no
	//! game Lua. So the panel lists what is knowable without a runtime and
	//! fills the individual tests in from a run's records. Listing a guess
	//! would be worse than listing nothing.
	//!
	//! The DECISION (which files are tests, which one wins a duplicate stable
	//! name, what order they come in) is @ref ScriptTestTools::collectTestFiles
	//! - the same rule the runner discovers with, so the panel and the run can
	//! never disagree about what the suite is. This only walks.
	//! An empty or missing directory yields an empty list.
	std::vector<Orkige::ScriptTestFile> scanProjectTests(
		Orkige::String const & projectRoot);
}

#endif //__EditorProjectTests_h__4_8_2026__12_00_00__
