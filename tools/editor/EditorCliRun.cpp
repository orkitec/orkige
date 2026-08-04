/********************************************************************
	created:	Monday 2026/08/03 at 16:00
	filename: 	EditorCliRun.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// The IMPURE half of the editor's command line: carry out a decision
// EditorCli.cpp already made (@see EditorCli.h). Runs entirely BEFORE the
// window, the render backend and the engine exist - the whole point is that a
// machine with no display can package a game.

#include "EditorApp.h"
#include "EditorBuildInfo.h"
#include "EditorCli.h"
#include "EditorPayloadFetcher.h"
#include "EditorResourcePaths.h"

#include <ExportProject.h>	// the manifest facts an export packages from

#include <core_http/HttpClient.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using Orkige::String;

	//! every human line carries the program's name, like the exporter's, so a
	//! script greps one prefix whichever door it came through
	void say(String const & message)
	{
		std::printf("orkige_editor: %s\n", message.c_str());
		std::fflush(stdout);
	}
	//---------------------------------------------------------
	int fail(String const & message)
	{
		std::printf("orkige_editor: ERROR: %s\n", message.c_str());
		std::fflush(stdout);
		return 1;
	}
	//---------------------------------------------------------
	//! `export`: the SAME plan-then-run pair the Build menu and the MCP verb
	//! use. Nothing is spawned and nothing is re-implemented - what this door
	//! adds is that it needs no window to walk through.
	int runExportCommand(OrkigeEditor::EditorCliCommand const & command)
	{
		// the manifest facts, read straight off disk. A live `Orkige::Project`
		// is deliberately NOT loaded: it would seize the process-wide active
		// AssetDatabase (@see ExportProject.h), and an export needs four facts,
		// not a world.
		OrkigeExport::ExportProject project;
		String error;
		if(!OrkigeExport::ExportProject::readManifest(command.projectPath,
			project, &error))
		{
			return fail(error);
		}
		const OrkigeEditor::EditorExportPlan plan =
			planExport(project, command.platform);
		if(!plan.ok)
		{
			// the plan's refusal is already the complete, actionable sentence -
			// the same one the menu shows on hover and the endpoint returns
			return fail(plan.error);
		}
		say("packaging '" + project.name + "' for " + plan.platform +
			" (engine: " + plan.enginePayload + ")");
		PlannedExportOverrides overrides;
		overrides.outputDirectory = command.outputDirectory;
		overrides.credentials = command.credentials;
		String artifact;
		if(!runPlannedExport(plan, project,
			[](std::string const & line) { say(line); },
			artifact, error, &overrides))
		{
			return fail(error);
		}
		// the machine-readable contract, spelled exactly like the exporter's so
		// a caller keys on one grep whichever door produced the artifact
		std::printf("orkige_editor: OK %s\n", artifact.c_str());
		std::fflush(stdout);
		return 0;
	}
	//---------------------------------------------------------
	//! `fetch-payload --list`: what this installation can install, and what it
	//! already has
	int listPayloads(OrkigeEditor::EditorPayloadFetcher const & fetcher)
	{
		const std::vector<OrkigeEditor::FetchablePayload> payloads =
			OrkigeEditor::fetchablePayloads();
		for(OrkigeEditor::FetchablePayload const & payload : payloads)
		{
			const String installed = fetcher.installedPath(payload.id);
			say(payload.id + " - " + payload.label + " - " +
				(installed.empty() ? String("not installed") : installed));
		}
		return 0;
	}
	//---------------------------------------------------------
	//! `fetch-payload <id>`: the download a released editor performs from its
	//! settings window, driven to completion here instead of over frames.
	//!
	//! @remarks The fetcher's `automatedRun` veto exists so a SCRIPTED EDITOR
	//! RUN is byte-identical to one on a machine with no network. This
	//! invocation is the opposite of incidental - a person or a build script
	//! named the download by id on the command line - so the fetcher is
	//! configured with the veto off. Nothing else about the run changes: no
	//! settings are read or written, and the install lands where a fetched
	//! payload always lands.
	int runFetchPayloadCommand(OrkigeEditor::EditorCliCommand const & command)
	{
		OrkigeEditor::EditorPayloadFetcher::Config config;
		config.version = Orkige::editorBuildVersion();
		config.flavor = ORKIGE_EDITOR_RENDER_BACKEND;
		config.rootDirectory =
			OrkigeEditor::editorWritableStateDirectory() + "payloads";
		OrkigeEditor::EditorPayloadFetcher fetcher(config);
		fetcher.setProcessRunner(
			[](std::vector<std::string> const & argv, std::string & output,
				int & exitCode)
			{
				return runProcessCaptured(argv, output, exitCode);
			});
		if(command.listPayloads)
		{
			return listPayloads(fetcher);
		}
		OrkigeEditor::FetchablePayload payload;
		if(!OrkigeEditor::findFetchablePayload(command.payloadId, payload))
		{
			return fail("'" + command.payloadId + "' is not a payload this "
				"build knows - run 'fetch-payload --list' to see them");
		}
		Orkige::HttpClient http;
		http.setUserAgent("orkige-editor/" +
			String(Orkige::editorBuildIdentity()));
		fetcher.setHttpClient(&http);
		String reason;
		if(!fetcher.canFetch(reason))
		{
			return fail(reason);
		}
		fetcher.beginFetch(command.payloadId);
		OrkigeEditor::PayloadFetchStage stage =
			OrkigeEditor::PayloadFetchStage::Idle;
		String lastMessage;
		// the same two pumps the frame loop runs, here in a plain loop: this
		// process has no frames to hang off
		for(;;)
		{
			http.update();
			fetcher.update();
			const OrkigeEditor::PayloadFetchStatus status = fetcher.status();
			if(status.stage != stage && !status.message.empty() &&
				status.message != lastMessage)
			{
				say(status.message);
				lastMessage = status.message;
			}
			stage = status.stage;
			if(stage == OrkigeEditor::PayloadFetchStage::Installed ||
				stage == OrkigeEditor::PayloadFetchStage::Failed)
			{
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}
		if(stage != OrkigeEditor::PayloadFetchStage::Installed)
		{
			return fail(fetcher.status().message);
		}
		std::printf("orkige_editor: OK %s\n",
			fetcher.installedPath(command.payloadId).c_str());
		std::fflush(stdout);
		return 0;
	}
}

//---------------------------------------------------------
namespace OrkigeEditor
{
	int runEditorCli(EditorCliCommand const & command)
	{
		if(command.usageError)
		{
			std::fprintf(stderr, "orkige_editor: %s\n", command.error.c_str());
			std::fprintf(stderr, "%s", editorCliUsage().c_str());
			std::fflush(stderr);
			return editorCliUsageExitCode();
		}
		switch(command.verb)
		{
		case EditorCliVerb::Help:
			std::printf("%s", editorCliUsage().c_str());
			std::fflush(stdout);
			return 0;
		case EditorCliVerb::Version:
			std::printf("%s\n", Orkige::editorVersionLine().c_str());
			std::fflush(stdout);
			return 0;
		case EditorCliVerb::Changelog:
		{
			const std::string & changelog = Orkige::editorBuildChangelog();
			std::printf("%s\n", changelog.empty()
				? Orkige::editorNoChangelogNote() : changelog.c_str());
			std::fflush(stdout);
			return 0;
		}
		case EditorCliVerb::Export:
			return runExportCommand(command);
		case EditorCliVerb::FetchPayload:
			return runFetchPayloadCommand(command);
		case EditorCliVerb::None:
			break;
		}
		// unreachable: main() only calls this for a headless command
		return 0;
	}
}
