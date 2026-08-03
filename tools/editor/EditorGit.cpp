/**************************************************************
	created:	2026/07/27 at 12:00
	filename: 	EditorGit.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "EditorGit.h"

#include <cstdlib>		// std::atoi
#include <filesystem>
#include <sstream>
#include <utility>		// std::move

namespace fs = std::filesystem;

namespace OrkigeEditor
{
	namespace
	{
		//! strip trailing CR/LF/space from a captured line or blob
		std::string trimTrailing(std::string value)
		{
			while (!value.empty() && (value.back() == '\n' || value.back() == '\r'
				|| value.back() == ' ' || value.back() == '\t'))
			{
				value.pop_back();
			}
			return value;
		}

		//! the substring of `line` starting AFTER its Nth space (1-based). Returns
		//! "" when the line has fewer than N spaces. Porcelain-v2 paths run to the
		//! end of the record, so the path is "everything after the last fixed
		//! field" - this pulls it without splitting on the spaces a path may hold.
		std::string afterNthSpace(std::string const& line, int n)
		{
			int seen = 0;
			for (std::size_t i = 0; i < line.size(); ++i)
			{
				if (line[i] == ' ')
				{
					if (++seen == n)
					{
						return line.substr(i + 1);
					}
				}
			}
			return std::string();
		}
	}

	std::vector<GitFileEntry> GitStatus::staged() const
	{
		std::vector<GitFileEntry> out;
		for (GitFileEntry const& entry : this->entries)
		{
			if (entry.isStaged())
			{
				out.push_back(entry);
			}
		}
		return out;
	}

	std::vector<GitFileEntry> GitStatus::unstaged() const
	{
		std::vector<GitFileEntry> out;
		for (GitFileEntry const& entry : this->entries)
		{
			if (entry.isUnstaged())
			{
				out.push_back(entry);
			}
		}
		return out;
	}

	std::vector<GitFileEntry> GitStatus::untrackedFiles() const
	{
		std::vector<GitFileEntry> out;
		for (GitFileEntry const& entry : this->entries)
		{
			if (entry.untracked)
			{
				out.push_back(entry);
			}
		}
		return out;
	}

	std::vector<GitFileEntry> GitStatus::conflicts() const
	{
		std::vector<GitFileEntry> out;
		for (GitFileEntry const& entry : this->entries)
		{
			if (entry.conflicted)
			{
				out.push_back(entry);
			}
		}
		return out;
	}

	bool GitStatus::anyStaged() const
	{
		for (GitFileEntry const& entry : this->entries)
		{
			if (entry.isStaged())
			{
				return true;
			}
		}
		return false;
	}

	GitStatus parseStatusPorcelainV2(std::string const& output)
	{
		GitStatus status;
		status.valid = true;	// a caller only parses a spawned+exited-0 result
		std::istringstream stream(output);
		std::string raw;
		while (std::getline(stream, raw))
		{
			const std::string line = trimTrailing(raw);
			if (line.empty())
			{
				continue;
			}
			if (line[0] == '#')
			{
				// branch header lines: "# branch.<key> <value...>"
				if (line.rfind("# branch.head ", 0) == 0)
				{
					const std::string value = line.substr(14);
					if (value == "(detached)")
					{
						status.detached = true;
					}
					else
					{
						status.branch = value;
					}
				}
				else if (line.rfind("# branch.oid ", 0) == 0)
				{
					status.initialCommit = line.substr(13) == "(initial)";
				}
				else if (line.rfind("# branch.upstream ", 0) == 0)
				{
					status.hasUpstream = true;
				}
				else if (line.rfind("# branch.ab ", 0) == 0)
				{
					// "+<ahead> -<behind>"
					std::istringstream ab(line.substr(12));
					std::string aheadTok;
					std::string behindTok;
					ab >> aheadTok >> behindTok;
					if (!aheadTok.empty() && aheadTok[0] == '+')
					{
						status.ahead = std::atoi(aheadTok.c_str() + 1);
					}
					if (!behindTok.empty() && behindTok[0] == '-')
					{
						status.behind = std::atoi(behindTok.c_str() + 1);
					}
				}
				continue;
			}
			GitFileEntry entry;
			if (line[0] == '1')
			{
				// "1 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <path>"
				if (line.size() < 3)
				{
					continue;
				}
				entry.index = line[2];
				entry.worktree = line.size() > 3 ? line[3] : '.';
				entry.path = afterNthSpace(line, 8);
			}
			else if (line[0] == '2')
			{
				// "2 <XY> ...8 fields... <path>\t<origPath>"
				if (line.size() < 3)
				{
					continue;
				}
				entry.index = line[2];
				entry.worktree = line.size() > 3 ? line[3] : '.';
				const std::string pair = afterNthSpace(line, 9);
				const std::size_t tab = pair.find('\t');
				if (tab != std::string::npos)
				{
					entry.path = pair.substr(0, tab);
					entry.origPath = pair.substr(tab + 1);
				}
				else
				{
					entry.path = pair;
				}
			}
			else if (line[0] == 'u')
			{
				// "u <XY> ...9 fields... <path>" - an unmerged (conflicted) path
				if (line.size() < 3)
				{
					continue;
				}
				entry.index = line[2];
				entry.worktree = line.size() > 3 ? line[3] : '.';
				entry.conflicted = true;
				entry.path = afterNthSpace(line, 10);
			}
			else if (line[0] == '?')
			{
				// "? <path>" - an untracked file
				entry.untracked = true;
				entry.path = afterNthSpace(line, 1);
			}
			else
			{
				// "!" (ignored) or an unrecognised/stderr line - skip
				continue;
			}
			if (!entry.path.empty())
			{
				status.entries.push_back(std::move(entry));
			}
		}
		return status;
	}

	GitBadge badgeForEntry(GitFileEntry const& entry)
	{
		if (entry.conflicted)
		{
			return GitBadge::Conflicted;
		}
		if (entry.untracked)
		{
			return GitBadge::Untracked;
		}
		// a worktree change is the louder signal (uncommitted, unstaged content);
		// a purely-staged path with a clean worktree reads as Staged
		if (entry.isUnstaged())
		{
			return GitBadge::Modified;
		}
		if (entry.isStaged())
		{
			return GitBadge::Staged;
		}
		return GitBadge::None;
	}

	std::map<std::string, GitBadge> buildBadgeMap(GitStatus const& status)
	{
		std::map<std::string, GitBadge> map;
		for (GitFileEntry const& entry : status.entries)
		{
			const GitBadge badge = badgeForEntry(entry);
			if (badge == GitBadge::None)
			{
				continue;
			}
			auto existing = map.find(entry.path);
			if (existing == map.end())
			{
				map.emplace(entry.path, badge);
			}
			else if (badge == GitBadge::Modified ||
				badge == GitBadge::Conflicted)
			{
				// Modified/Conflicted win over a Staged entry for the same path
				existing->second = badge;
			}
		}
		return map;
	}

	std::set<std::string> collectDirtyFolders(
		std::vector<std::string> const& repoRelPaths)
	{
		std::set<std::string> folders;
		for (std::string const& path : repoRelPaths)
		{
			// every '/' marks an ancestor folder boundary
			std::size_t slash = path.find('/');
			while (slash != std::string::npos)
			{
				folders.insert(path.substr(0, slash));
				slash = path.find('/', slash + 1);
			}
		}
		return folders;
	}

	std::string joinRepoRelative(std::string const& projectPrefix,
		std::string const& projectRelative)
	{
		if (projectPrefix.empty())
		{
			return projectRelative;
		}
		if (projectRelative.empty())
		{
			return projectPrefix;
		}
		return projectPrefix + "/" + projectRelative;
	}

	GitBadge GitBadgeSnapshot::badgeForProjectPath(
		std::string const& projectRelative) const
	{
		if (!this->active)
		{
			return GitBadge::None;
		}
		const std::string repoRel =
			joinRepoRelative(this->projectPrefix, projectRelative);
		auto found = this->fileBadges.find(repoRel);
		return found == this->fileBadges.end() ? GitBadge::None : found->second;
	}

	bool GitBadgeSnapshot::folderDirtyForProjectPath(
		std::string const& projectRelFolder) const
	{
		if (!this->active)
		{
			return false;
		}
		const std::string repoRel =
			joinRepoRelative(this->projectPrefix, projectRelFolder);
		if (repoRel.empty())
		{
			// the repo root as a folder: dirty if anything at all is dirty
			return !this->fileBadges.empty();
		}
		return this->dirtyFolders.count(repoRel) != 0;
	}

	GitBadgeSnapshot buildBadgeSnapshot(GitStatus const& status,
		std::string const& repoRoot, std::string const& projectRoot)
	{
		GitBadgeSnapshot snapshot;
		if (!status.valid || repoRoot.empty())
		{
			return snapshot;	// inactive
		}
		snapshot.active = true;
		snapshot.repoRoot = repoRoot;
		// the project's path inside the repo ("" when the project IS the repo)
		snapshot.projectPrefix = gitRepoRelative(repoRoot, projectRoot);
		snapshot.fileBadges = buildBadgeMap(status);
		std::vector<std::string> dirtyPaths;
		dirtyPaths.reserve(snapshot.fileBadges.size());
		for (auto const& pair : snapshot.fileBadges)
		{
			dirtyPaths.push_back(pair.first);
		}
		snapshot.dirtyFolders = collectDirtyFolders(dirtyPaths);
		return snapshot;
	}

	std::string gitResolveRepoRoot(GitRunner const& run,
		std::string const& pathInRepo, bool* gitPresent)
	{
		if (!run || pathInRepo.empty())
		{
			return std::string();	// no probe made: presence stays unknown
		}
		std::error_code ec;
		// resolve against a DIRECTORY: git -C needs one, and a file's parent is
		// inside the same repo
		std::string dir = pathInRepo;
		if (!fs::is_directory(pathInRepo, ec))
		{
			dir = fs::path(pathInRepo).parent_path().string();
		}
		if (dir.empty())
		{
			return std::string();
		}
		std::string output;
		int exitCode = 0;
		// the SPAWN result and the EXIT code answer different questions: a failed
		// spawn means git is not on this machine, a non-zero exit means git ran
		// and said no. Both yield "" here, so the distinction is reported out
		// rather than collapsed - the panel tells the user which one it is.
		const bool spawned =
			run({ "git", "-C", dir, "rev-parse", "--show-toplevel" },
				output, exitCode);
		if (gitPresent)
		{
			*gitPresent = spawned;
		}
		if (!spawned || exitCode != 0)
		{
			return std::string();	// git absent, or this path is not a repo
		}
		return trimTrailing(output);
	}

	std::string gitRepoRelative(std::string const& repoRoot,
		std::string const& absolutePath)
	{
		if (repoRoot.empty() || absolutePath.empty())
		{
			return std::string();
		}
		std::error_code ec;
		const std::string rel =
			fs::relative(absolutePath, repoRoot, ec).generic_string();
		if (ec || rel.empty() || rel == "." || rel.rfind("..", 0) == 0)
		{
			return std::string();	// outside the repo
		}
		return rel;
	}

	//--- GitRepo operations ---------------------------------------------------

	namespace
	{
		//! run `git -C <root> <args...>` through the runner into a GitResult
		GitResult runGit(GitRepo const& repo, std::vector<std::string> args)
		{
			GitResult result;
			std::vector<std::string> argv = { "git", "-C", repo.root };
			argv.insert(argv.end(), args.begin(), args.end());
			result.spawned = repo.run(argv, result.output, result.exitCode);
			result.output = trimTrailing(result.output);
			return result;
		}
	}

	GitStatus GitRepo::status() const
	{
		if (!this->valid())
		{
			return GitStatus();
		}
		// -uall lists untracked files INDIVIDUALLY (git's default collapses a new
		// folder to one entry, which would hide the browser's per-file dots);
		// core.quotePath=false emits non-ASCII paths raw (UTF-8) so a localised
		// asset name parses verbatim (a path with a literal quote/newline still
		// arrives C-quoted - the documented v1 limitation).
		const GitResult result = runGit(*this, { "-c", "core.quotePath=false",
			"status", "--porcelain=v2", "--branch", "--untracked-files=all" });
		if (!result.ok())
		{
			return GitStatus();	// invalid: not a repo / git failure
		}
		return parseStatusPorcelainV2(result.output);
	}

	GitResult GitRepo::stage(std::string const& repoRel) const
	{
		return runGit(*this, { "add", "--", repoRel });
	}

	GitResult GitRepo::unstage(std::string const& repoRel) const
	{
		return runGit(*this, { "reset", "-q", "--", repoRel });
	}

	GitResult GitRepo::stageAll() const
	{
		return runGit(*this, { "add", "-A" });
	}

	GitResult GitRepo::unstageAll() const
	{
		return runGit(*this, { "reset", "-q" });
	}

	GitResult GitRepo::discard(std::string const& repoRel) const
	{
		// reset the file to its COMMITTED (HEAD) content - clears staged AND
		// unstaged edits. Destructive; the caller confirms first.
		return runGit(*this, { "checkout", "HEAD", "--", repoRel });
	}

	GitResult GitRepo::commit(std::string const& message) const
	{
		return runGit(*this, { "commit", "-m", message });
	}

	GitResult GitRepo::push() const
	{
		return runGit(*this, { "push" });
	}

	GitResult GitRepo::publishBranch(std::string const& branch) const
	{
		return runGit(*this, { "push", "-u", "origin", branch });
	}

	std::string GitRepo::showStagedBlob(std::string const& repoRel,
		int& exitCode) const
	{
		exitCode = -1;
		if (!this->valid())
		{
			return std::string();
		}
		std::string output;
		if (!this->run({ "git", "-C", this->root, "show", ":" + repoRel },
			output, exitCode))
		{
			exitCode = -1;
			return std::string();
		}
		return output;
	}
}
