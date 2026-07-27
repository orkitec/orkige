/**************************************************************
	created:	2026/07/27 at 12:00
	filename: 	EditorGit.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#ifndef __EditorGit_h__27_7_2026__12_00_00__
#define __EditorGit_h__27_7_2026__12_00_00__

//! @file EditorGit.h
//! @brief the editor's one git seam: a PURE `git status --porcelain=v2 --branch`
//! parser (unit-tested), a per-path badge model + folder aggregation the Source
//! Control panel AND the Asset browser share (one snapshot, never two git
//! invocations for the same data), and thin runner-injected repo operations
//! (root resolution, stage/unstage/discard/commit/push/blob). The subprocess is
//! injected as a `GitRunner` so this whole unit stays free of the editor's SDL
//! process helper and is drivable headlessly against a throwaway temp repo.
//!
//! The CLI is the house route (no libgit2); the editor wires runProcessCaptured
//! as the runner, capturing stdout+stderr merged so a commit-msg hook rejection
//! or a push auth error surfaces honestly.

#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace OrkigeEditor
{
	//! @brief inject-a-subprocess seam. Runs `argv` (argv[0] is the executable,
	//! e.g. "git"), captures the child's combined stdout+stderr into `output` and
	//! its exit code into `exitCode`. Returns false ONLY when the process could
	//! not be spawned (git absent from PATH) - a non-zero exit is a true return
	//! with the message in `output`. The editor supplies runProcessCaptured;
	//! selfchecks supply the same real runner against a temp repo.
	using GitRunner = std::function<bool(std::vector<std::string> const& argv,
		std::string& output, int& exitCode)>;

	//! @brief the outcome of one git operation (stage/commit/push/...): whether
	//! it ran and exited 0, plus the child's combined output for the status line.
	struct GitResult
	{
		bool		spawned = false;	//!< the process launched (git present)
		int			exitCode = 0;		//!< the child's exit code (valid if spawned)
		std::string	output;				//!< combined stdout+stderr (trimmed)

		//! ran AND succeeded (git present and exited 0)
		bool ok() const { return this->spawned && this->exitCode == 0; }
	};

	//! @brief one changed path in a porcelain-v2 status. The index (X) and
	//! worktree (Y) status letters follow git's own vocabulary ('.' = unmodified,
	//! M/A/D/R/C/T/U); untracked and conflicted are flagged distinctly since git
	//! reports them on their own record types ('?' and 'u').
	struct GitFileEntry
	{
		std::string	path;			//!< repo-relative, forward slashes
		std::string	origPath;		//!< rename/copy SOURCE (repo-rel), else ""
		char		index = '.';	//!< X - the staged (index vs HEAD) state
		char		worktree = '.';	//!< Y - the unstaged (worktree vs index) state
		bool		untracked = false;	//!< a '?' record (never in the index)
		bool		conflicted = false;	//!< a 'u' record (an unmerged path)

		//! has a staged change (an index entry differing from HEAD)
		bool isStaged() const
		{
			return !this->untracked && !this->conflicted && this->index != '.';
		}
		//! has an UNSTAGED worktree change (worktree differing from the index)
		bool isUnstaged() const
		{
			return !this->untracked && !this->conflicted && this->worktree != '.';
		}
		//! a rename/copy (carries an origPath)
		bool isRename() const { return !this->origPath.empty(); }
	};

	//! @brief the coarse per-path indicator the Asset browser draws: one dot
	//! state per file (the panel uses the richer per-group X/Y directly). Colour
	//! mapping is the browser's (untracked = green, conflicted = red, staged /
	//! modified = amber - a cheap staged-vs-modified distinction is kept here).
	enum class GitBadge : unsigned char
	{
		None,		//!< clean / not reported
		Modified,	//!< a worktree change (may also be partly staged)
		Staged,		//!< an index change with a clean worktree
		Untracked,	//!< a new file git does not track
		Conflicted	//!< an unmerged path
	};

	//! @brief a parsed `git status --porcelain=v2 --branch`. `valid` is false for
	//! a non-repo / git-absent result; the file lists are grouped by the panel.
	struct GitStatus
	{
		bool		valid = false;			//!< parsed from a real repo
		std::string	branch;					//!< current branch ("" if detached)
		bool		detached = false;		//!< HEAD is detached
		bool		initialCommit = false;	//!< no commits yet (unborn HEAD)
		bool		hasUpstream = false;	//!< an upstream is configured
		int			ahead = 0;				//!< commits ahead of upstream
		int			behind = 0;				//!< commits behind upstream
		std::vector<GitFileEntry>	entries;//!< every changed/untracked/unmerged path

		//! entries with a staged (index) change - the "Staged" group
		std::vector<GitFileEntry> staged() const;
		//! tracked entries with an unstaged worktree change - the "Changes" group
		std::vector<GitFileEntry> unstaged() const;
		//! untracked files - the "Untracked" group
		std::vector<GitFileEntry> untrackedFiles() const;
		//! unmerged paths - conflicts (their own group / status-line note)
		std::vector<GitFileEntry> conflicts() const;
		//! is anything staged? (the Commit button gate)
		bool anyStaged() const;
		//! nothing changed, untracked or conflicted (a clean tree)
		bool clean() const { return this->entries.empty(); }
	};

	//! @brief PURE: parse `git status --porcelain=v2 --branch` output into a
	//! GitStatus. Tolerant of a trailing stderr line (the runner merges streams);
	//! an unrecognised line is skipped. Empty / all-header input parses to a valid
	//! clean status. NUL-delimited (-z) is NOT assumed - paths run to end-of-line
	//! (porcelain v2 without -z leaves them unquoted for the common case; a path
	//! with an embedded newline is the documented v1 limitation, out of scope).
	GitStatus parseStatusPorcelainV2(std::string const& output);

	//! @brief PURE: the browser dot state for one entry.
	GitBadge badgeForEntry(GitFileEntry const& entry);

	//! @brief PURE: repo-relative path -> badge for every changed path (the ONE
	//! snapshot the panel and the browser share). A path present in both the
	//! staged and worktree sets resolves to Modified (uncommitted worktree content
	//! is the louder signal). Rename entries badge their CURRENT path.
	std::map<std::string, GitBadge> buildBadgeMap(GitStatus const& status);

	//! @brief PURE: every ancestor FOLDER (repo-relative, forward slashes, no
	//! trailing slash) of any dirty path - so an Asset browser folder row can show
	//! an aggregate dot when a descendant is dirty. "a/b/c.lua" contributes "a" and
	//! "a/b". The repo root itself is the empty string and is NOT included.
	std::set<std::string> collectDirtyFolders(
		std::vector<std::string> const& repoRelPaths);

	//! @brief PURE: join a repo-internal project PREFIX (the project root's path
	//! relative to the repo root, forward slashes, no trailing slash - "" when the
	//! project IS the repo root) with a project-relative path into one repo-
	//! relative path. Normalises the separator; an empty rel returns the prefix.
	std::string joinRepoRelative(std::string const& projectPrefix,
		std::string const& projectRelative);

	//! @brief the ONE cached status view the Source Control panel computes and the
	//! Asset browser reads (never a second git invocation for the same data): the
	//! per-file badges + aggregated dirty folders, both repo-relative, plus the
	//! project's PREFIX inside the repo so a browser query in PROJECT-relative
	//! coordinates resolves without re-running git. PURE to build + query.
	struct GitBadgeSnapshot
	{
		bool		active = false;		//!< a repo resolved + a status computed
		std::string	repoRoot;			//!< absolute repo root
		std::string	projectPrefix;		//!< repoRoot -> projectRoot ("" = same)
		std::map<std::string, GitBadge>	fileBadges;		//!< repo-rel -> badge
		std::set<std::string>			dirtyFolders;	//!< repo-rel dirty folders

		//! the badge for a PROJECT-relative file path (the browser's coordinate);
		//! None when clean / not tracked / the snapshot is inactive
		GitBadge badgeForProjectPath(std::string const& projectRelative) const;
		//! does a PROJECT-relative FOLDER hold any dirty descendant? (the browser's
		//! folder-row aggregate dot)
		bool folderDirtyForProjectPath(std::string const& projectRelFolder) const;
	};

	//! @brief PURE: fold a parsed status into the shared snapshot. `repoRoot` and
	//! `projectRoot` are absolute; the project prefix is derived (a project may sit
	//! deep in the repo). An invalid status yields an inactive snapshot.
	GitBadgeSnapshot buildBadgeSnapshot(GitStatus const& status,
		std::string const& repoRoot, std::string const& projectRoot);

	//! @brief resolve the git repo root that contains `pathInRepo` (an ABSOLUTE
	//! file or directory path) via `git -C <dir> rev-parse --show-toplevel`. "" on
	//! a non-repo / git-absent / bad path. Uses the passed absolute path, never the
	//! process cwd (a .app bundle's cwd is not the source tree). @see GitRunner.
	std::string gitResolveRepoRoot(GitRunner const& run,
		std::string const& pathInRepo);

	//! @brief the repo-relative path (forward slashes) of an ABSOLUTE path under
	//! `repoRoot`; "" when the path is outside the repo. PURE (a lexical/std::fs
	//! relate, no subprocess).
	std::string gitRepoRelative(std::string const& repoRoot,
		std::string const& absolutePath);

	//! @brief a resolved repository the panel/selfcheck drives operations on. All
	//! ops build `git -C <root> ...` argv and run through the injected runner, so
	//! the whole surface is exercisable headlessly against a temp repo.
	struct GitRepo
	{
		GitRunner	run;	//!< the injected subprocess runner
		std::string	root;	//!< absolute repo root ("" = not resolved)

		bool valid() const
		{
			return static_cast<bool>(this->run) && !this->root.empty();
		}

		//! `git status --porcelain=v2 --branch` -> parsed GitStatus (invalid on a
		//! spawn/exit failure).
		GitStatus status() const;
		//! `git add -- <path>` (stages a modification, addition or deletion)
		GitResult stage(std::string const& repoRel) const;
		//! `git reset -q -- <path>` (unstages; works before the first commit too)
		GitResult unstage(std::string const& repoRel) const;
		//! `git add -A` (stage every change - the group "Stage All")
		GitResult stageAll() const;
		//! `git reset -q` (unstage everything - the group "Unstage All")
		GitResult unstageAll() const;
		//! `git checkout HEAD -- <path>`: reset the file to its COMMITTED content,
		//! discarding staged AND unstaged edits. DESTRUCTIVE - the caller confirms.
		GitResult discard(std::string const& repoRel) const;
		//! `git commit -m <message>`: the repo's own hooks run naturally; a
		//! commit-msg hook rejection lands in the result's output (exitCode != 0).
		GitResult commit(std::string const& message) const;
		//! `git push`: inherits the user's credential setup; auth/network failure
		//! lands in the result's output (from stderr).
		GitResult push() const;
		//! `git push -u origin <branch>`: publish a branch that has no upstream yet
		//! (the first push), setting `origin` as its upstream. A missing/differently
		//! named remote fails honestly in the result's output.
		GitResult publishBranch(std::string const& branch) const;
		//! `git show :<path>` - the STAGED (index) blob of a path (the diff-gutter
		//! baseline). exitCode is set; an untracked path exits non-zero.
		std::string showStagedBlob(std::string const& repoRel, int& exitCode) const;
	};
}

#endif //__EditorGit_h__27_7_2026__12_00_00__
