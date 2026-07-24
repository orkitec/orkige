// ScriptBreakpoints - the editor's per-project script breakpoint store.
//
// Breakpoints are keyed (project-relative script path, 1-based line) - the
// exact currency the ScriptRuntime debugger matches chunk names against and
// the debug protocol carries as "<file>:<line>". The store PERSISTS per
// project across editor sessions in "<projectRoot>/.orkige/breakpoints"
// (one formatted entry per line - a project-scoped sidecar like the
// autosave, NOT part of any scene; the .orkige/ folder is gitignored).
// Pure filesystem + string logic, no UI/engine dependency, so it lives in
// the UI-independent orkige_editor_core library and is unit-tested headlessly.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <core_script/ScriptDebugCore.h>
#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	//! @brief the editor-side breakpoint set + its per-project persistence.
	//! attachProject() loads the project's saved set (and remembers where to
	//! save); every mutation saves back immediately (the set is tiny). With no
	//! project attached the store still works, just without persistence.
	class ScriptBreakpointStore
	{
	public:
		//! the persistence file, relative to the project root
		static constexpr char const* STORE_RELATIVE_PATH =
			".orkige/breakpoints";

		//! @brief bind to a project root: forget the current set, load the
		//! project's persisted one (a missing file = empty set). An empty root
		//! detaches (clears, stops persisting).
		void attachProject(String const& projectRoot);
		//! the attached project root ("" = none)
		String const& projectRoot() const { return mProjectRoot; }

		//! set (idempotent) - returns true when the set changed
		bool set(String const& file, int line);
		//! clear one - returns true when it existed
		bool clear(String const& file, int line);
		//! toggle - returns the breakpoint's NEW presence state
		bool toggle(String const& file, int line);
		//! drop every breakpoint - returns true when any existed
		bool clearAll();
		bool has(String const& file, int line) const;
		//! every breakpoint, in a stable (file, line) order
		std::vector<ScriptBreakpoint> const& list() const { return mEntries; }
		//! the 1-based lines with a breakpoint in ONE file (the gutter query)
		std::vector<int> linesFor(String const& file) const;
		//! the wire form: one "<file>:<line>" entry per breakpoint (the
		//! MSG_DEBUG_BREAKPOINTS full-replace list)
		std::vector<String> wireList() const;
		//! a change counter the play session watches to re-send the set
		unsigned int revision() const { return mRevision; }

	private:
		//! keep mEntries sorted (file, then line) so persistence is stable
		void sortEntries();
		//! write the set to the attached project's store file (a no-op without
		//! a project; the .orkige/ folder is created on first use)
		void save() const;
		//! read the attached project's store file into the set
		void load();

		String mProjectRoot;
		std::vector<ScriptBreakpoint> mEntries;
		unsigned int mRevision = 0;
	};
}
