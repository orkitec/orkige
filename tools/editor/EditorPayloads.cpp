/********************************************************************
	created:	Sunday 2026/08/02 at 14:00
	filename: 	EditorPayloads.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorPayloads - the decision table behind the fetched pieces of an
// installation (see the header).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorPayloads.h"

#include <core_util/HelpLink.h>

#include <core_util/VersionOrder.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <functional>

namespace OrkigeEditor
{
	namespace
	{
		//! the ids, spelled once. They travel in published file names and in
		//! install paths, so they are frozen the moment one is published.
		char const * const PAYLOAD_IOS_SIMULATOR = "player-ios-simulator";
		char const * const PAYLOAD_ANDROID = "player-android";

		//! the prebuilt player inside a player payload, and the engine media
		//! beside it - the same `Media/` layout every runtime resolves at boot
		char const * const IOS_PLAYER_APP = "OrkigePlayer.app";
		char const * const ANDROID_PLAYER_LIB = "libmain.so";
		char const * const PAYLOAD_MEDIA_DIR = "Media";
		//! the Android payload's assembly half: the packaging script, the
		//! manifest template it substitutes, and the Java it compiles. An
		//! Android package is assembled AROUND the player rather than copied
		//! whole like a `.app`, so those pieces are ENGINE and travel with it -
		//! only the SDK's own programs stay the machine's.
		char const * const ANDROID_ASSEMBLY_DIR = "android";
		//! the manifest a payload describes itself with (platform, flavor,
		//! version). The EXPORTER reads it to learn which flavor it is
		//! packaging - the name is spelled there too
		//! (OrkigeExport::DEVICE_PAYLOAD_MANIFEST_FILE_NAME), because the two
		//! libraries do not link each other; a unit test on each side pins the
		//! literal so the pair cannot drift.
		char const * const PAYLOAD_MANIFEST_FILE = "orkige_payload.txt";

		Orkige::String trim(Orkige::String const & text)
		{
			std::size_t begin = 0;
			std::size_t end = text.size();
			while(begin < end &&
				std::isspace(static_cast<unsigned char>(text[begin])) != 0)
			{
				++begin;
			}
			while(end > begin &&
				std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
			{
				--end;
			}
			return text.substr(begin, end - begin);
		}
		//---------------------------------------------------------
		//! `<directory>/<leaf>` with exactly one separator between them
		Orkige::String join(Orkige::String const & directory,
			Orkige::String const & leaf)
		{
			if(directory.empty())
			{
				return leaf;
			}
			if(leaf.empty())
			{
				return directory;
			}
			const char last = directory[directory.size() - 1];
			return (last == '/' || last == '\\') ? (directory + leaf)
				: (directory + "/" + leaf);
		}
		//---------------------------------------------------------
		//! does @p path end in @p suffix (a case-sensitive extension test)
		bool endsWith(Orkige::String const & path, char const * suffix)
		{
			const Orkige::String tail(suffix);
			return path.size() >= tail.size() &&
				path.compare(path.size() - tail.size(), tail.size(), tail) == 0;
		}
	}
	//---------------------------------------------------------
	std::vector<FetchablePayload> fetchablePayloads()
	{
		std::vector<FetchablePayload> payloads;

		FetchablePayload ios;
		ios.id = PAYLOAD_IOS_SIMULATOR;
		ios.label = "iOS Simulator player";
		ios.platformLabel = "iOS Simulator";
		ios.kind = PayloadKind::Player;
		ios.exportPlatform = "ios-simulator";
		ios.marker = join(IOS_PLAYER_APP, "Info.plist");
		payloads.push_back(ios);

		FetchablePayload android;
		android.id = PAYLOAD_ANDROID;
		android.label = "Android player";
		android.platformLabel = "Android";
		android.kind = PayloadKind::Player;
		android.exportPlatform = "android";
		android.marker = ANDROID_PLAYER_LIB;
		// an APK is ASSEMBLED rather than copied, so this payload carries the
		// assembler and the Java it compiles beside the player - everything
		// that belongs to the engine. The Android SDK's own programs are the
		// machine's, reported one by one when one is missing, which is the
		// same line we draw for a native module's cmake and ninja.
		android.extraPaths.push_back(
			join(ANDROID_ASSEMBLY_DIR, "package_apk.sh"));
		android.extraPaths.push_back(
			join(ANDROID_ASSEMBLY_DIR, "AndroidManifest.xml"));
		android.extraPaths.push_back(
			join(ANDROID_ASSEMBLY_DIR, "java"));
		payloads.push_back(android);
		return payloads;
	}
	//---------------------------------------------------------
	bool findFetchablePayload(Orkige::String const & id, FetchablePayload & out)
	{
		const std::vector<FetchablePayload> payloads = fetchablePayloads();
		for(std::size_t index = 0; index < payloads.size(); ++index)
		{
			if(payloads[index].id == id)
			{
				out = payloads[index];
				return true;
			}
		}
		return false;
	}
	//---------------------------------------------------------
	Orkige::String payloadIdForExportPlatform(Orkige::String const & platform)
	{
		const std::vector<FetchablePayload> payloads = fetchablePayloads();
		for(std::size_t index = 0; index < payloads.size(); ++index)
		{
			if(!payloads[index].exportPlatform.empty() &&
				payloads[index].exportPlatform == platform)
			{
				return payloads[index].id;
			}
		}
		return Orkige::String();
	}
	//---------------------------------------------------------
	Orkige::String payloadAssetName(Orkige::String const & id,
		Orkige::String const & flavor, Orkige::String const & version)
	{
		const Orkige::String token = Orkige::VersionOrder::filenameToken(version);
		if(id.empty() || flavor.empty() || token.empty())
		{
			return Orkige::String();
		}
		return "orkige-" + id + "-" + flavor + "-" + token + ".zip";
	}
	//---------------------------------------------------------
	Orkige::String payloadReleaseTag(Orkige::String const & version)
	{
		// the dated tag is composed from the day inside the ordered version
		// ("2.0.0-nightly.20260802+abc" -> "nightly-20260802"), which is the
		// SAME composition the publishing side performs from the same date
		const Orkige::String marker = "nightly.";
		const std::size_t at = version.find(marker);
		if(at == Orkige::String::npos)
		{
			return Orkige::String();
		}
		const std::size_t begin = at + marker.size();
		if(version.size() < begin + 8)
		{
			return Orkige::String();
		}
		const Orkige::String day = version.substr(begin, 8);
		for(std::size_t index = 0; index < day.size(); ++index)
		{
			if(std::isdigit(static_cast<unsigned char>(day[index])) == 0)
			{
				return Orkige::String();
			}
		}
		// what follows the day must END the release identity, not continue it:
		// "nightly.202608021" is not a day this client understands
		if(version.size() > begin + 8)
		{
			const char next = version[begin + 8];
			if(next != '+' && next != '_')
			{
				return Orkige::String();
			}
		}
		return "nightly-" + day;
	}
	//---------------------------------------------------------
	char const * defaultPayloadReleasesUrl()
	{
		return "https://api.github.com/repos/orkitec/orkige/releases";
	}
	//---------------------------------------------------------
	Orkige::String payloadReleaseUrl(Orkige::String const & base,
		Orkige::String const & version)
	{
		const Orkige::String tag = payloadReleaseTag(version);
		if(tag.empty())
		{
			return Orkige::String();
		}
		const Orkige::String collection =
			base.empty() ? Orkige::String(defaultPayloadReleasesUrl()) : base;
		return join(collection, "tags/" + tag);
	}
	//---------------------------------------------------------
	Orkige::String payloadInstallDirectory(Orkige::String const & root,
		Orkige::String const & id, Orkige::String const & flavor,
		Orkige::String const & version)
	{
		const Orkige::String token = Orkige::VersionOrder::filenameToken(version);
		if(root.empty() || id.empty() || flavor.empty() || token.empty())
		{
			return Orkige::String();
		}
		return join(join(join(root, id), flavor), token);
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> planPayloadPrune(
		std::vector<InstalledPayload> const & installed,
		std::vector<Orkige::String> const & enabledIds,
		Orkige::String const & flavor, Orkige::String const & version)
	{
		const Orkige::String token = Orkige::VersionOrder::filenameToken(version);
		std::vector<Orkige::String> doomed;
		for(std::size_t index = 0; index < installed.size(); ++index)
		{
			InstalledPayload const & entry = installed[index];
			const bool wanted = isPayloadEnabled(enabledIds, entry.id) &&
				entry.flavor == flavor && !token.empty() &&
				entry.version == token;
			if(!wanted && !entry.path.empty())
			{
				doomed.push_back(entry.path);
			}
		}
		return doomed;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> payloadRequiredPaths(
		FetchablePayload const & payload)
	{
		std::vector<Orkige::String> required;
		// the self-description first: a payload that cannot say which flavor
		// it is would be packaged as the wrong one
		required.push_back(PAYLOAD_MANIFEST_FILE);
		if(!payload.marker.empty())
		{
			required.push_back(payload.marker);
		}
		for(std::size_t index = 0; index < payload.extraPaths.size(); ++index)
		{
			required.push_back(payload.extraPaths[index]);
		}
		if(payload.kind == PayloadKind::Player)
		{
			// a player renders through the engine media staged beside it: the
			// payload is self-contained precisely so an export from it needs
			// no repository and no build tree
			required.push_back(PAYLOAD_MEDIA_DIR);
		}
		return required;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> payloadProblems(
		FetchablePayload const & payload, Orkige::String const & directory,
		std::function<bool(Orkige::String const &)> const & exists)
	{
		std::vector<Orkige::String> problems;
		const std::vector<Orkige::String> required =
			payloadRequiredPaths(payload);
		for(std::size_t index = 0; index < required.size(); ++index)
		{
			if(!exists || !exists(join(directory, required[index])))
			{
				problems.push_back(required[index]);
			}
		}
		return problems;
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> parseEnabledPayloads(
		Orkige::String const & text)
	{
		std::vector<Orkige::String> ids;
		std::size_t begin = 0;
		while(begin <= text.size())
		{
			const std::size_t comma = text.find(',', begin);
			const Orkige::String token = trim(text.substr(begin,
				comma == Orkige::String::npos ? Orkige::String::npos
					: comma - begin));
			FetchablePayload payload;
			if(!token.empty() && findFetchablePayload(token, payload) &&
				!isPayloadEnabled(ids, token))
			{
				ids.push_back(token);
			}
			if(comma == Orkige::String::npos)
			{
				break;
			}
			begin = comma + 1;
		}
		return ids;
	}
	//---------------------------------------------------------
	Orkige::String formatEnabledPayloads(
		std::vector<Orkige::String> const & ids)
	{
		// catalogue order, so the same set always persists as the same line
		Orkige::String text;
		const std::vector<FetchablePayload> payloads = fetchablePayloads();
		for(std::size_t index = 0; index < payloads.size(); ++index)
		{
			if(isPayloadEnabled(ids, payloads[index].id))
			{
				text += (text.empty() ? "" : ",") + payloads[index].id;
			}
		}
		return text;
	}
	//---------------------------------------------------------
	bool isPayloadEnabled(std::vector<Orkige::String> const & enabled,
		Orkige::String const & id)
	{
		return std::find(enabled.begin(), enabled.end(), id) != enabled.end();
	}
	//---------------------------------------------------------
	std::vector<Orkige::String> payloadExtractCommand(
		Orkige::String const & archivePath, Orkige::String const & destination)
	{
		std::vector<Orkige::String> command;
		if(archivePath.empty() || destination.empty() ||
			!endsWith(archivePath, ".zip"))
		{
			return command;
		}
#if defined(__APPLE__)
		// the tool the archive was made with: an application bundle's
		// executable bits have to survive the round trip, and an unpacked
		// player that cannot be executed is not a player
		command.push_back("/usr/bin/ditto");
		command.push_back("-x");
		command.push_back("-k");
		command.push_back(archivePath);
		command.push_back(destination);
#elif defined(_WIN32)
		command.push_back("tar.exe");
		command.push_back("-xf");
		command.push_back(archivePath);
		command.push_back("-C");
		command.push_back(destination);
#else
		command.push_back("unzip");
		command.push_back("-q");
		command.push_back("-o");
		command.push_back(archivePath);
		command.push_back("-d");
		command.push_back(destination);
#endif
		return command;
	}
	//---------------------------------------------------------
	Orkige::String payloadMissingMessage(FetchablePayload const & payload,
		bool enabled, bool hasNetworkClient)
	{
		// where the whole mechanism is written down. Every sentence below ends
		// on it, because "cannot export" with no reading is only half an
		// answer - and it is composed from a DOC NAME, so a renamed page
		// breaks doc_link_lint rather than shipping a dead link.
		const Orkige::String reading =
			" - more in " + Orkige::helpUrl(payloadHelpPage());
		if(!enabled)
		{
			return "packaging for " + payload.platformLabel + " is not switched "
				"on for this installation - enable it under Settings > Build "
				"Targets, and Orkige fetches the " + payload.label + " once" +
				reading;
		}
		if(!hasNetworkClient)
		{
			return "the " + payload.label + " is not installed, and this build "
				"carries no network transport to fetch it with - install an "
				"Orkige build with HTTP support, or package from an engine "
				"source tree" + reading;
		}
		return "the " + payload.label + " is not installed yet - fetch it under "
			"Settings > Build Targets (one download, kept for this version of "
			"Orkige), then export again" + reading;
	}
	//---------------------------------------------------------
	char const * payloadHelpPage()
	{
		return "device-payloads";
	}
}
