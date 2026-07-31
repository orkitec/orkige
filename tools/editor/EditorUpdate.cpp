/********************************************************************
	created:	Friday 2026/07/31 at 09:00
	filename: 	EditorUpdate.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorUpdate - the updater's decision table (see the header).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorUpdate.h"

#include <core_debugnet/Json.h>
#include <core_util/VersionOrder.h>

#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>

namespace OrkigeEditor
{
	namespace
	{
		//! the marker the release notes carry the ordered version in
		char const* const VERSION_MARKER_OPEN = "<!-- orkige-nightly-version:";
		char const* const MARKER_CLOSE = "-->";
		//! the heading the changelog section starts at
		char const* const CHANGELOG_HEADING = "## Changes since";
		//! the digest sidecar's extension
		char const* const CHECKSUM_SUFFIX = ".sha256";

		//! trim ASCII whitespace from both ends
		std::string trim(std::string const& text)
		{
			std::size_t begin = 0;
			std::size_t end = text.size();
			while (begin < end &&
				std::isspace(static_cast<unsigned char>(text[begin])) != 0)
			{
				++begin;
			}
			while (end > begin &&
				std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
			{
				--end;
			}
			return text.substr(begin, end - begin);
		}

		//! is the whole of @p text a 64-character lower/upper hex digest
		bool looksLikeDigest(std::string const& text)
		{
			if (text.size() != 64)
			{
				return false;
			}
			for (std::size_t index = 0; index < text.size(); ++index)
			{
				const unsigned char raw =
					static_cast<unsigned char>(text[index]);
				const bool hex = (raw >= '0' && raw <= '9') ||
					(raw >= 'a' && raw <= 'f') || (raw >= 'A' && raw <= 'F');
				if (!hex)
				{
					return false;
				}
			}
			return true;
		}

		std::string toLower(std::string const& text)
		{
			std::string lowered;
			lowered.reserve(text.size());
			for (std::size_t index = 0; index < text.size(); ++index)
			{
				lowered += static_cast<char>(std::tolower(
					static_cast<unsigned char>(text[index])));
			}
			return lowered;
		}

		//! the last path separator in @p path (either slash), or npos
		std::size_t lastSeparator(std::string const& path)
		{
			const std::size_t slash = path.find_last_of("/\\");
			return slash;
		}

		//! @p path with any trailing separators removed (a directory named
		//! with and without one is the same directory)
		std::string stripTrailingSeparators(std::string const& path)
		{
			std::size_t end = path.size();
			while (end > 1 && (path[end - 1] == '/' || path[end - 1] == '\\'))
			{
				--end;
			}
			return path.substr(0, end);
		}

		//! turn the ordered version into the token filenames carry
		std::string fileToken(std::string const& version)
		{
			return Orkige::VersionOrder::filenameToken(version);
		}
	}
	//---------------------------------------------------------
	char const* updatePolicyName(UpdatePolicy policy)
	{
		switch (policy)
		{
		case UpdatePolicy::Off:			return "off";
		case UpdatePolicy::Download:	return "download";
		case UpdatePolicy::Notify:		break;
		}
		return "notify";
	}
	//---------------------------------------------------------
	char const* updatePolicyLabel(UpdatePolicy policy)
	{
		switch (policy)
		{
		case UpdatePolicy::Off:
			return "Never check";
		case UpdatePolicy::Download:
			return "Check and download in the background";
		case UpdatePolicy::Notify:
			break;
		}
		return "Check and tell me";
	}
	//---------------------------------------------------------
	UpdatePolicy parseUpdatePolicy(std::string const& text,
		UpdatePolicy fallback)
	{
		const std::string token = toLower(trim(text));
		if (token == "off" || token == "never" || token == "0")
		{
			return UpdatePolicy::Off;
		}
		if (token == "download" || token == "auto")
		{
			return UpdatePolicy::Download;
		}
		if (token == "notify" || token == "check" || token == "1")
		{
			return UpdatePolicy::Notify;
		}
		return fallback;
	}
	//---------------------------------------------------------
	long long updateCheckIntervalSeconds()
	{
		return 24LL * 60LL * 60LL;
	}
	//---------------------------------------------------------
	UpdateCheckDecision decideUpdateCheck(UpdateCheckContext const& context)
	{
		// an automated run is the ONE absolute veto: no network, no user
		// state, not even when something asked explicitly
		if (context.automatedRun)
		{
			return UpdateCheckDecision::Automated;
		}
		if (!context.hasOrderedVersion)
		{
			// nothing to compare against - saying so beats both offering an
			// update and claiming to be current
			return UpdateCheckDecision::NoOrderedVersion;
		}
		if (context.manual)
		{
			// an explicit click is consent: it overrides both the Off setting
			// and the interval
			return UpdateCheckDecision::Run;
		}
		if (context.policy == UpdatePolicy::Off)
		{
			return UpdateCheckDecision::PolicyOff;
		}
		if (context.lastCheckEpochSeconds > 0)
		{
			const long long elapsed =
				context.nowEpochSeconds - context.lastCheckEpochSeconds;
			// a stamp in the future (a clock that moved backwards) counts as
			// "long enough ago" rather than locking checking out until the
			// clock catches up
			if (elapsed >= 0 && elapsed < updateCheckIntervalSeconds())
			{
				return UpdateCheckDecision::TooSoon;
			}
		}
		return UpdateCheckDecision::Run;
	}
	//---------------------------------------------------------
	char const* updateCheckDecisionReason(UpdateCheckDecision decision)
	{
		switch (decision)
		{
		case UpdateCheckDecision::PolicyOff:
			return "Update checks are switched off.";
		case UpdateCheckDecision::Automated:
			return "Automated runs never check for updates.";
		case UpdateCheckDecision::TooSoon:
			return "Already checked within the last day.";
		case UpdateCheckDecision::NoOrderedVersion:
			return "This build carries no version number to compare - "
				"updates are only offered to released builds.";
		case UpdateCheckDecision::Run:
			break;
		}
		return "";
	}
	//---------------------------------------------------------
	UpdateRelease parseUpdateRelease(std::string const& json)
	{
		UpdateRelease release;
		Orkige::JsonValue document;
		if (!Orkige::JsonValue::parse(json, document) || !document.isObject())
		{
			release.problem = "The update service answered with something "
				"that is not a release.";
			return release;
		}
		Orkige::JsonValue const& assets = document.get("assets");
		for (std::size_t index = 0; index < assets.size(); ++index)
		{
			Orkige::JsonValue const& entry = assets.at(index);
			UpdateAsset asset;
			asset.name = entry.get("name").asString();
			asset.url = entry.get("browser_download_url").asString();
			const double size = entry.get("size").asNumber(0.0);
			asset.size = size > 0.0
				? static_cast<unsigned long long>(size) : 0ull;
			if (!asset.empty())
			{
				release.assets.push_back(asset);
			}
		}

		const std::string body = document.get("body").asString();
		const std::size_t markerBegin = body.find(VERSION_MARKER_OPEN);
		if (markerBegin == std::string::npos)
		{
			release.problem = "The published release carries no version "
				"marker, so there is nothing to compare against.";
			return release;
		}
		const std::size_t valueBegin =
			markerBegin + std::string(VERSION_MARKER_OPEN).size();
		const std::size_t markerEnd = body.find(MARKER_CLOSE, valueBegin);
		if (markerEnd == std::string::npos)
		{
			release.problem = "The published release's version marker is "
				"incomplete.";
			return release;
		}
		release.version =
			trim(body.substr(valueBegin, markerEnd - valueBegin));
		if (release.version.empty())
		{
			release.problem = "The published release names no version.";
			return release;
		}

		// the changelog section, from its heading to the markers at the end
		const std::size_t sectionBegin = body.find(CHANGELOG_HEADING);
		if (sectionBegin != std::string::npos)
		{
			std::size_t sectionEnd = body.find("<!--", sectionBegin);
			if (sectionEnd == std::string::npos)
			{
				sectionEnd = body.size();
			}
			release.changelog =
				trim(body.substr(sectionBegin, sectionEnd - sectionBegin));
		}
		release.valid = true;
		return release;
	}
	//---------------------------------------------------------
	UpdatePlatform hostUpdatePlatform()
	{
#if defined(_WIN32)
		return UpdatePlatform::Windows;
#elif defined(__APPLE__)
		return UpdatePlatform::MacOS;
#else
		return UpdatePlatform::Linux;
#endif
	}
	//---------------------------------------------------------
	std::string updateArchiveName(UpdatePlatform platform,
		std::string const& version)
	{
		const std::string token = fileToken(version);
		if (token.empty())
		{
			return std::string();
		}
		switch (platform)
		{
		case UpdatePlatform::MacOS:
			return "Orkige-macos-" + token + ".zip";
		case UpdatePlatform::Linux:
			return "Orkige-linux-" + token + ".tar.gz";
		case UpdatePlatform::Windows:
			return "Orkige-windows-" + token + ".zip";
		}
		return std::string();
	}
	//---------------------------------------------------------
	UpdateAssetChoice selectUpdateAssets(
		std::vector<UpdateAsset> const& assets, UpdatePlatform platform,
		std::string const& version)
	{
		UpdateAssetChoice choice;
		const std::string wanted = updateArchiveName(platform, version);
		if (wanted.empty())
		{
			choice.problem = "The published version cannot be turned into a "
				"download name.";
			return choice;
		}
		const std::string wantedChecksum = wanted + CHECKSUM_SUFFIX;
		for (std::size_t index = 0; index < assets.size(); ++index)
		{
			// an exact name match, so an install shape - a disk image, an
			// installer - can never be picked up by a prefix or a suffix
			if (assets[index].name == wanted)
			{
				choice.archive = assets[index];
			}
			else if (assets[index].name == wantedChecksum)
			{
				choice.checksum = assets[index];
			}
		}
		if (choice.archive.empty())
		{
			choice.problem = "This release has no download for this platform ("
				+ wanted + ").";
			return choice;
		}
		if (choice.checksum.empty())
		{
			// the sidecar IS the download's integrity story; without it there
			// is nothing to check the bytes against, so the update stops here
			choice.problem = "The download has no checksum file beside it ("
				+ wantedChecksum + ").";
			return choice;
		}
		choice.found = true;
		return choice;
	}
	//---------------------------------------------------------
	UpdateVerdict judgeUpdate(std::string const& published,
		std::string const& current)
	{
		using namespace Orkige::VersionOrder;
		switch (compare(published, current))
		{
		case VO_NEWER:	return UpdateVerdict::Offer;
		case VO_SAME:	return UpdateVerdict::UpToDate;
		case VO_OLDER:	return UpdateVerdict::Older;
		case VO_INCOMPARABLE:
			break;
		}
		return UpdateVerdict::Incomparable;
	}
	//---------------------------------------------------------
	char const* updateVerdictReason(UpdateVerdict verdict)
	{
		switch (verdict)
		{
		case UpdateVerdict::Offer:
			return "A newer version is available.";
		case UpdateVerdict::UpToDate:
			return "You are on the latest version.";
		case UpdateVerdict::Older:
			return "The published build is older than this one - "
				"nothing to install.";
		case UpdateVerdict::Incomparable:
			break;
		}
		return "The published version cannot be compared with this build.";
	}
	//---------------------------------------------------------
	std::string parseChecksumSidecar(std::string const& text,
		std::string const& fileName)
	{
		std::istringstream lines(text);
		std::string line;
		std::string bareDigest;
		while (std::getline(lines, line))
		{
			const std::string trimmed = trim(line);
			if (trimmed.empty())
			{
				continue;
			}
			// "<digest>  <name>" (the checking tool's own format) - or a
			// digest on its own line, which is the other common shape
			const std::size_t space = trimmed.find_first_of(" \t");
			if (space == std::string::npos)
			{
				if (looksLikeDigest(trimmed) && bareDigest.empty())
				{
					bareDigest = toLower(trimmed);
				}
				continue;
			}
			const std::string digest = trimmed.substr(0, space);
			if (!looksLikeDigest(digest))
			{
				continue;
			}
			std::string named = trim(trimmed.substr(space));
			// the binary-mode marker the checking tool writes
			if (!named.empty() && named[0] == '*')
			{
				named = trim(named.substr(1));
			}
			// the sidecar may name the file with a leading directory; the
			// last component is what has to match
			const std::size_t slash = lastSeparator(named);
			if (slash != std::string::npos)
			{
				named = named.substr(slash + 1);
			}
			if (named == fileName)
			{
				return toLower(digest);
			}
		}
		// a bare digest names nothing, so it is only usable when the sidecar
		// carried no named entry at all
		return bareDigest;
	}
	//---------------------------------------------------------
	bool isTranslocatedPath(std::string const& path)
	{
		return path.find("/AppTranslocation/") != std::string::npos;
	}
	//---------------------------------------------------------
	InstallLocationVerdict judgeInstallLocation(
		InstallLocationFacts const& facts)
	{
		if (facts.installPath.empty() || !facts.installExists)
		{
			return InstallLocationVerdict::Missing;
		}
		// order matters: a translocated or build-tree path is refused for its
		// OWN reason even when the container happens to be writable, because
		// what the user has to be told differs in each case
		if (facts.translocated || isTranslocatedPath(facts.installPath))
		{
			return InstallLocationVerdict::Translocated;
		}
		if (facts.insideBuildTree)
		{
			return InstallLocationVerdict::BuildTree;
		}
		if (facts.containerPath.empty() || !facts.containerWritable)
		{
			return InstallLocationVerdict::ReadOnly;
		}
		return InstallLocationVerdict::Updatable;
	}
	//---------------------------------------------------------
	char const* installLocationReason(InstallLocationVerdict verdict)
	{
		switch (verdict)
		{
		case InstallLocationVerdict::Missing:
			return "This editor's own location could not be resolved - "
				"download the new version from the releases page.";
		case InstallLocationVerdict::ReadOnly:
			return "This editor is installed somewhere it cannot replace "
				"itself - download the new version from the releases page.";
		case InstallLocationVerdict::Translocated:
			return "This editor is running from a temporary read-only copy. "
				"Move it to your Applications folder, or download the new "
				"version from the releases page.";
		case InstallLocationVerdict::BuildTree:
			return "This editor was built from source - update the source "
				"tree and rebuild instead.";
		case InstallLocationVerdict::Updatable:
			break;
		}
		return "";
	}
	//---------------------------------------------------------
	UpdateSwapPlan planUpdateSwap(std::string const& installedPath,
		std::string const& stagedPath, std::string const& backupSuffix)
	{
		UpdateSwapPlan plan;
		plan.installedPath = stripTrailingSeparators(trim(installedPath));
		plan.stagedPath = stripTrailingSeparators(trim(stagedPath));
		if (plan.installedPath.empty() || plan.stagedPath.empty())
		{
			plan.problem = "The swap needs both an installed and a staged "
				"path.";
			return plan;
		}
		if (plan.installedPath == plan.stagedPath)
		{
			plan.problem = "The staged copy and the installed one are the "
				"same path.";
			return plan;
		}
		const std::size_t slash = lastSeparator(plan.installedPath);
		if (slash == std::string::npos || slash == 0)
		{
			// a bare name has no container to rename inside, and a top-level
			// entry is not something this may rearrange
			plan.problem = "The installed path has no directory to swap "
				"inside.";
			return plan;
		}
		const std::string suffix = trim(backupSuffix);
		if (suffix.empty() ||
			suffix.find_first_of("/\\") != std::string::npos)
		{
			plan.problem = "The backup suffix is empty or carries a path "
				"separator.";
			return plan;
		}
		// the backup is a SIBLING of the installed copy, so both moves are
		// renames inside one directory: neither can half-copy, and the undo
		// is exactly the first rename reversed
		plan.backupPath = plan.installedPath + ".orkige-previous-" + suffix;
		plan.valid = true;
		return plan;
	}
	//---------------------------------------------------------
	std::string shellQuotePosix(std::string const& text)
	{
		std::string quoted = "'";
		for (std::size_t index = 0; index < text.size(); ++index)
		{
			if (text[index] == '\'')
			{
				// close, escape a literal quote, reopen
				quoted += "'\\''";
			}
			else
			{
				quoted += text[index];
			}
		}
		quoted += "'";
		return quoted;
	}
	//---------------------------------------------------------
	std::string shellQuoteWindows(std::string const& text)
	{
		for (std::size_t index = 0; index < text.size(); ++index)
		{
			const unsigned char raw = static_cast<unsigned char>(text[index]);
			// a command script has no escape for these; refusing beats
			// emitting a line that would mean something else
			if (raw == '"' || raw == '%' || raw == '^' || raw == '&' ||
				raw == '|' || raw == '<' || raw == '>' || raw < 0x20)
			{
				return std::string();
			}
		}
		return "\"" + text + "\"";
	}
	//---------------------------------------------------------
	char const* updateHelperFileName(UpdatePlatform platform)
	{
		return platform == UpdatePlatform::Windows
			? "orkige-update.cmd" : "orkige-update.sh";
	}
	//---------------------------------------------------------
	std::vector<std::string> updateHelperCommand(UpdatePlatform platform,
		std::string const& scriptPath)
	{
		std::vector<std::string> command;
		if (scriptPath.empty())
		{
			return command;
		}
		if (platform == UpdatePlatform::Windows)
		{
			command.push_back("cmd.exe");
			command.push_back("/c");
			command.push_back(scriptPath);
		}
		else
		{
			command.push_back("/bin/sh");
			command.push_back(scriptPath);
		}
		return command;
	}
	//---------------------------------------------------------
	std::vector<std::string> updateExtractCommand(UpdatePlatform platform,
		std::string const& archivePath, std::string const& destination)
	{
		std::vector<std::string> command;
		if (archivePath.empty() || destination.empty())
		{
			return command;
		}
		switch (platform)
		{
		case UpdatePlatform::MacOS:
			// the same tool the archive was made with: a bundle's symlinks
			// and executable bits have to survive the round trip
			command.push_back("/usr/bin/ditto");
			command.push_back("-x");
			command.push_back("-k");
			command.push_back(archivePath);
			command.push_back(destination);
			break;
		case UpdatePlatform::Linux:
			command.push_back("tar");
			command.push_back("-xzf");
			command.push_back(archivePath);
			command.push_back("-C");
			command.push_back(destination);
			break;
		case UpdatePlatform::Windows:
			command.push_back("tar.exe");
			command.push_back("-xf");
			command.push_back(archivePath);
			command.push_back("-C");
			command.push_back(destination);
			break;
		}
		return command;
	}
	//---------------------------------------------------------
	std::vector<std::string> updateSignatureVerifyCommand(
		UpdatePlatform platform, std::string const& payloadPath)
	{
		std::vector<std::string> command;
		// macOS is the one platform whose published builds carry a signature
		// today; elsewhere the digest is the whole check, and the caller says
		// so rather than pretending the platforms are equal
		if (platform != UpdatePlatform::MacOS || payloadPath.empty())
		{
			return command;
		}
		command.push_back("/usr/bin/codesign");
		command.push_back("--verify");
		command.push_back("--strict");
		command.push_back("--deep");
		command.push_back(payloadPath);
		return command;
	}
	//---------------------------------------------------------
	std::vector<std::string> updateSignatureAssessCommand(
		UpdatePlatform platform, std::string const& payloadPath)
	{
		std::vector<std::string> command;
		if (platform != UpdatePlatform::MacOS || payloadPath.empty())
		{
			return command;
		}
		command.push_back("/usr/sbin/spctl");
		command.push_back("--assess");
		command.push_back("--type");
		command.push_back("exec");
		command.push_back(payloadPath);
		return command;
	}
	//---------------------------------------------------------
	char const* updatePayloadName(UpdatePlatform platform)
	{
		return platform == UpdatePlatform::MacOS ? "Orkige.app" : "orkige";
	}
	//---------------------------------------------------------
	namespace
	{
		//! the POSIX helper: wait for the editor, swap, undo on failure
		std::string composePosixHelper(UpdateHelperSpec const& spec)
		{
			const std::string installed =
				shellQuotePosix(spec.plan.installedPath);
			const std::string staged = shellQuotePosix(spec.plan.stagedPath);
			const std::string backup = shellQuotePosix(spec.plan.backupPath);
			std::ostringstream script;
			script <<
				"#!/bin/sh\n"
				"# Orkige update helper. Written by the editor, run once, "
				"removes itself.\n"
				"# It decides nothing: the download was checked against its "
				"digest and its\n"
				"# signature before this file existed. All it does is wait, "
				"move, move, and\n"
				"# put the first move back if the second one fails.\n"
				"set -u\n"
				"PID=" << spec.pid << "\n"
				"WAITED=0\n"
				"LIMIT=" << spec.waitTimeoutSeconds << "\n"
				"while [ \"$PID\" -gt 0 ] && kill -0 \"$PID\" 2>/dev/null; do\n"
				"  if [ \"$WAITED\" -ge \"$LIMIT\" ]; then\n"
				"    echo \"orkige-update: the editor is still running after "
				"${LIMIT}s - nothing was changed\" >&2\n"
				"    exit 2\n"
				"  fi\n"
				"  WAITED=$((WAITED + 1))\n"
				"  sleep 1\n"
				"done\n"
				"INSTALLED=" << installed << "\n"
				"STAGED=" << staged << "\n"
				"BACKUP=" << backup << "\n"
				"if [ ! -e \"$STAGED\" ]; then\n"
				"  echo \"orkige-update: the staged copy is gone - nothing was "
				"changed\" >&2\n"
				"  exit 3\n"
				"fi\n"
				"rm -rf \"$BACKUP\"\n"
				"if ! mv \"$INSTALLED\" \"$BACKUP\"; then\n"
				"  echo \"orkige-update: could not move the installed copy "
				"aside - nothing was changed\" >&2\n"
				"  exit 4\n"
				"fi\n"
				"if ! mv \"$STAGED\" \"$INSTALLED\"; then\n"
				"  # the half-swapped state is the one thing that must never "
				"be left behind\n"
				"  mv \"$BACKUP\" \"$INSTALLED\"\n"
				"  echo \"orkige-update: the new version could not be moved "
				"in - the previous one was put back\" >&2\n"
				"  exit 5\n"
				"fi\n"
				"rm -rf \"$BACKUP\"\n";
			if (!spec.relaunchPath.empty())
			{
				const std::string relaunch =
					shellQuotePosix(spec.relaunchPath);
				if (spec.platform == UpdatePlatform::MacOS)
				{
					script << "/usr/bin/open " << relaunch << " || true\n";
				}
				else
				{
					script << relaunch << " >/dev/null 2>&1 &\n";
				}
			}
			script << "rm -f \"$0\"\n"
				"exit 0\n";
			return script.str();
		}

		//! the Windows helper - the same five steps in the one scripting
		//! language a plain Windows install is guaranteed to have
		std::string composeWindowsHelper(UpdateHelperSpec const& spec)
		{
			const std::string installed =
				shellQuoteWindows(spec.plan.installedPath);
			const std::string staged =
				shellQuoteWindows(spec.plan.stagedPath);
			const std::string backup =
				shellQuoteWindows(spec.plan.backupPath);
			if (installed.empty() || staged.empty() || backup.empty())
			{
				return std::string();
			}
			std::string relaunch;
			if (!spec.relaunchPath.empty())
			{
				relaunch = shellQuoteWindows(spec.relaunchPath);
				if (relaunch.empty())
				{
					return std::string();
				}
			}
			std::ostringstream script;
			script <<
				"@echo off\r\n"
				"rem Orkige update helper. Written by the editor, run once, "
				"removes itself.\r\n"
				"rem It decides nothing: the download was checked against its "
				"digest before\r\n"
				"rem this file existed. All it does is wait, move, move, and "
				"put the first\r\n"
				"rem move back if the second one fails.\r\n"
				"setlocal\r\n"
				"set WAITED=0\r\n"
				":wait\r\n"
				"tasklist /fi \"PID eq " << spec.pid <<
					"\" 2>nul | find \"" << spec.pid << "\" >nul\r\n"
				"if errorlevel 1 goto swap\r\n"
				"if %WAITED% GEQ " << spec.waitTimeoutSeconds <<
					" goto stillrunning\r\n"
				"set /a WAITED=%WAITED%+1\r\n"
				"ping -n 2 127.0.0.1 >nul\r\n"
				"goto wait\r\n"
				":stillrunning\r\n"
				"echo orkige-update: the editor is still running - nothing "
				"was changed 1>&2\r\n"
				"exit /b 2\r\n"
				":swap\r\n"
				"if not exist " << staged << " (\r\n"
				"  echo orkige-update: the staged copy is gone - nothing was "
				"changed 1>&2\r\n"
				"  exit /b 3\r\n"
				")\r\n"
				"if exist " << backup << " rmdir /s /q " << backup << "\r\n"
				"move " << installed << " " << backup << " >nul\r\n"
				"if errorlevel 1 (\r\n"
				"  echo orkige-update: could not move the installed copy "
				"aside - nothing was changed 1>&2\r\n"
				"  exit /b 4\r\n"
				")\r\n"
				"move " << staged << " " << installed << " >nul\r\n"
				"if errorlevel 1 (\r\n"
				"  move " << backup << " " << installed << " >nul\r\n"
				"  echo orkige-update: the new version could not be moved in "
				"- the previous one was put back 1>&2\r\n"
				"  exit /b 5\r\n"
				")\r\n"
				"rmdir /s /q " << backup << " 2>nul\r\n";
			if (!relaunch.empty())
			{
				script << "start \"\" " << relaunch << "\r\n";
			}
			script << "del \"%~f0\"\r\n"
				"exit /b 0\r\n";
			return script.str();
		}
	}
	//---------------------------------------------------------
	std::string composeUpdateHelperScript(UpdateHelperSpec const& spec)
	{
		if (!spec.plan.valid)
		{
			return std::string();
		}
		if (spec.platform == UpdatePlatform::Windows)
		{
			return composeWindowsHelper(spec);
		}
		return composePosixHelper(spec);
	}
	//---------------------------------------------------------
	char const* updateStageLabel(UpdateStage stage)
	{
		switch (stage)
		{
		case UpdateStage::Checking:		return "Checking for updates";
		case UpdateStage::UpToDate:		return "Up to date";
		case UpdateStage::Available:	return "Update available";
		case UpdateStage::Downloading:	return "Downloading update";
		case UpdateStage::Verifying:	return "Verifying update";
		case UpdateStage::Ready:		return "Update ready";
		case UpdateStage::Failed:		return "Update failed";
		case UpdateStage::Idle:			break;
		}
		return "";
	}
	//---------------------------------------------------------
}
