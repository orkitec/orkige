/********************************************************************
	created:	2026/07/27 at 12:00
	filename: 	EditorGitTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorGitTests - the Source Control panel's PURE git seam: the
// `git status --porcelain=v2 --branch` parser battery (every record + branch
// header line), the per-path badge model + the map merge, the Asset browser's
// dirty-folder aggregation and the repo-relative path helpers. No subprocess -
// the GitRepo operations run against a real temp repo in the editor selfcheck.
#include <catch2/catch_test_macros.hpp>

#include <EditorGit.h>

#include <algorithm>

using namespace OrkigeEditor;

namespace
{
	bool hasPath(std::vector<GitFileEntry> const& list, std::string const& path)
	{
		return std::any_of(list.begin(), list.end(),
			[&](GitFileEntry const& e) { return e.path == path; });
	}
}

TEST_CASE("porcelain-v2: empty / header-only parses to a valid clean status",
	"[unit][editor][git]")
{
	const GitStatus empty = parseStatusPorcelainV2("");
	CHECK(empty.valid);
	CHECK(empty.clean());
	CHECK(empty.entries.empty());

	const GitStatus headerOnly = parseStatusPorcelainV2(
		"# branch.oid 1a2b3c\n"
		"# branch.head main\n"
		"# branch.upstream origin/main\n"
		"# branch.ab +0 -0\n");
	CHECK(headerOnly.valid);
	CHECK(headerOnly.clean());
	CHECK(headerOnly.branch == "main");
	CHECK(headerOnly.hasUpstream);
	CHECK(headerOnly.ahead == 0);
	CHECK(headerOnly.behind == 0);
	CHECK_FALSE(headerOnly.detached);
	CHECK_FALSE(headerOnly.initialCommit);
}

TEST_CASE("porcelain-v2: branch ahead/behind, detached and initial-commit",
	"[unit][editor][git]")
{
	const GitStatus ab = parseStatusPorcelainV2(
		"# branch.head feature/x\n"
		"# branch.upstream origin/feature/x\n"
		"# branch.ab +3 -2\n");
	CHECK(ab.branch == "feature/x");
	CHECK(ab.ahead == 3);
	CHECK(ab.behind == 2);
	CHECK(ab.hasUpstream);

	const GitStatus detached = parseStatusPorcelainV2("# branch.head (detached)\n");
	CHECK(detached.detached);
	CHECK(detached.branch.empty());

	const GitStatus initial = parseStatusPorcelainV2(
		"# branch.oid (initial)\n# branch.head main\n");
	CHECK(initial.initialCommit);
	CHECK_FALSE(initial.hasUpstream);
}

TEST_CASE("porcelain-v2: ordinary (type 1) records - staged/unstaged/both",
	"[unit][editor][git]")
{
	// XY: '.'=clean. M.=staged only, .M=unstaged only, MM=both, A.=added-staged,
	// .D=deleted-in-worktree
	const GitStatus status = parseStatusPorcelainV2(
		"# branch.head main\n"
		"1 M. N... 100644 100644 100644 aaa bbb staged_only.lua\n"
		"1 .M N... 100644 100644 100644 aaa aaa unstaged_only.lua\n"
		"1 MM N... 100644 100644 100644 aaa bbb both.lua\n"
		"1 A. N... 000000 100644 100644 000 ccc added.txt\n"
		"1 .D N... 100644 100644 000000 ddd ddd removed.txt\n");
	REQUIRE(status.entries.size() == 5);

	CHECK(hasPath(status.staged(), "staged_only.lua"));
	CHECK(hasPath(status.staged(), "both.lua"));
	CHECK(hasPath(status.staged(), "added.txt"));
	CHECK_FALSE(hasPath(status.staged(), "unstaged_only.lua"));

	CHECK(hasPath(status.unstaged(), "unstaged_only.lua"));
	CHECK(hasPath(status.unstaged(), "both.lua"));
	CHECK(hasPath(status.unstaged(), "removed.txt"));
	CHECK_FALSE(hasPath(status.unstaged(), "staged_only.lua"));

	CHECK(status.anyStaged());
	CHECK_FALSE(status.clean());
}

TEST_CASE("porcelain-v2: rename (type 2) carries the current + original path",
	"[unit][editor][git]")
{
	const GitStatus status = parseStatusPorcelainV2(
		"# branch.head main\n"
		"2 R. N... 100644 100644 100644 aaa aaa R100 new/name.lua\told/name.lua\n");
	REQUIRE(status.entries.size() == 1);
	GitFileEntry const& entry = status.entries.front();
	CHECK(entry.path == "new/name.lua");
	CHECK(entry.origPath == "old/name.lua");
	CHECK(entry.isRename());
	CHECK(entry.isStaged());
}

TEST_CASE("porcelain-v2: unmerged (type u) is conflicted, untracked (?) and "
	"ignored (!)", "[unit][editor][git]")
{
	const GitStatus status = parseStatusPorcelainV2(
		"# branch.head main\n"
		"u UU N... 100644 100644 100644 100644 a b c merged/conflict.lua\n"
		"? brand_new.png\n"
		"! ignored/thing.o\n");
	// the ignored '!' line is skipped
	REQUIRE(status.entries.size() == 2);
	CHECK(hasPath(status.conflicts(), "merged/conflict.lua"));
	CHECK(hasPath(status.untrackedFiles(), "brand_new.png"));
	// a conflicted path is neither "staged" nor "unstaged" for the panel groups
	CHECK_FALSE(hasPath(status.staged(), "merged/conflict.lua"));
	CHECK_FALSE(hasPath(status.unstaged(), "merged/conflict.lua"));
}

TEST_CASE("porcelain-v2: paths containing spaces survive", "[unit][editor][git]")
{
	const GitStatus status = parseStatusPorcelainV2(
		"1 .M N... 100644 100644 100644 aaa aaa dir with spaces/a file.lua\n"
		"? new asset name.png\n");
	REQUIRE(status.entries.size() == 2);
	CHECK(status.entries[0].path == "dir with spaces/a file.lua");
	CHECK(status.entries[1].path == "new asset name.png");
}

TEST_CASE("porcelain-v2: raw UTF-8 paths survive (core.quotePath=false)",
	"[unit][editor][git]")
{
	// git is invoked with core.quotePath=false, so a localised asset name
	// arrives as raw UTF-8 bytes rather than octal-escaped; the parser takes the
	// path verbatim to end-of-line
	const GitStatus status = parseStatusPorcelainV2(
		"? assets/h\xc3\xa9ros/\xe6\x95\x8c.png\n");
	REQUIRE(status.entries.size() == 1);
	CHECK(status.entries[0].path == "assets/h\xc3\xa9ros/\xe6\x95\x8c.png");
	CHECK(status.entries[0].untracked);
}

TEST_CASE("badge model: per-entry + the merged map", "[unit][editor][git]")
{
	GitFileEntry untracked;
	untracked.untracked = true;
	CHECK(badgeForEntry(untracked) == GitBadge::Untracked);

	GitFileEntry conflicted;
	conflicted.conflicted = true;
	CHECK(badgeForEntry(conflicted) == GitBadge::Conflicted);

	GitFileEntry stagedOnly;
	stagedOnly.index = 'M';
	CHECK(badgeForEntry(stagedOnly) == GitBadge::Staged);

	GitFileEntry worktreeDirty;
	worktreeDirty.index = 'M';
	worktreeDirty.worktree = 'M';
	CHECK(badgeForEntry(worktreeDirty) == GitBadge::Modified);

	const GitStatus status = parseStatusPorcelainV2(
		"1 M. N... 100644 100644 100644 a b s.lua\n"
		"1 .M N... 100644 100644 100644 a a w.lua\n"
		"? u.png\n"
		"u UU N... 100644 100644 100644 100644 a b c k.lua\n");
	const std::map<std::string, GitBadge> map = buildBadgeMap(status);
	CHECK(map.at("s.lua") == GitBadge::Staged);
	CHECK(map.at("w.lua") == GitBadge::Modified);
	CHECK(map.at("u.png") == GitBadge::Untracked);
	CHECK(map.at("k.lua") == GitBadge::Conflicted);
}

TEST_CASE("dirty-folder aggregation: every ancestor, no root, no trailing slash",
	"[unit][editor][git]")
{
	const std::set<std::string> folders = collectDirtyFolders({
		"a/b/c.lua",
		"a/d.txt",
		"top_level.png",		// no folder ancestors -> contributes nothing
		"x/y/z/deep.png"
	});
	CHECK(folders.count("a") == 1);
	CHECK(folders.count("a/b") == 1);
	CHECK(folders.count("x") == 1);
	CHECK(folders.count("x/y") == 1);
	CHECK(folders.count("x/y/z") == 1);
	// a top-level file has no ancestor folder; the empty repo root is never listed
	CHECK(folders.count("") == 0);
	CHECK(folders.count("top_level.png") == 0);
	// the file paths themselves are NOT folders
	CHECK(folders.count("a/b/c.lua") == 0);
}

TEST_CASE("joinRepoRelative: project-prefix + project-relative -> repo-relative",
	"[unit][editor][git]")
{
	// the project sits deep in the repo
	CHECK(joinRepoRelative("games/myproj", "scripts/player.lua")
		== "games/myproj/scripts/player.lua");
	// the project IS the repo root (no prefix)
	CHECK(joinRepoRelative("", "scripts/player.lua") == "scripts/player.lua");
	// an empty rel resolves to the prefix (the project folder itself)
	CHECK(joinRepoRelative("games/myproj", "") == "games/myproj");
}

TEST_CASE("gitRepoRelative: an absolute path under the root; outside -> empty",
	"[unit][editor][git]")
{
	CHECK(gitRepoRelative("/repo/root", "/repo/root/a/b.lua") == "a/b.lua");
	// the root itself is not a relative file path
	CHECK(gitRepoRelative("/repo/root", "/repo/root").empty());
	// outside the repo
	CHECK(gitRepoRelative("/repo/root", "/elsewhere/x.lua").empty());
	// empty inputs
	CHECK(gitRepoRelative("", "/repo/root/a.lua").empty());
	CHECK(gitRepoRelative("/repo/root", "").empty());
}

TEST_CASE("GitRepo is invalid without a runner or a root", "[unit][editor][git]")
{
	GitRepo none;
	CHECK_FALSE(none.valid());
	CHECK_FALSE(none.status().valid);

	GitRepo noRoot;
	noRoot.run = [](std::vector<std::string> const&, std::string&, int&)
		{ return true; };
	CHECK_FALSE(noRoot.valid());
}

TEST_CASE("gitResolveRepoRoot returns empty when git cannot spawn",
	"[unit][editor][git]")
{
	// a runner that reports the process could not launch (git absent)
	GitRunner absent = [](std::vector<std::string> const&, std::string&, int&)
		{ return false; };
	CHECK(gitResolveRepoRoot(absent, "/some/dir").empty());
	// a null runner / empty path
	CHECK(gitResolveRepoRoot(GitRunner(), "/some/dir").empty());
	CHECK(gitResolveRepoRoot(absent, "").empty());
}
