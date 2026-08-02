/********************************************************************
	created:	Thursday 2026/07/09 at 12:00
	filename: 	EditorExport.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorExport.cpp - the async project export job (Build menu), run IN PROCESS
// on a worker thread with its progress streamed into the Console.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "EditorEngineSdk.h"
#include "EditorExportPlan.h"
#include "EditorPayloads.h"
#include "EditorResourcePaths.h"

#include <core_project/NativeModule.h>

#include <ExportProject.h>
#include <ExportRun.h>

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>

//! @brief what this project + platform packages, or the refusal (@see
//! EditorExportPlan.h). Every path is resolved through the ONE resource
//! locator: an editor built in the source tree packages a preset build tree,
//! a COPIED app packages the engine payload it carries inside itself.
OrkigeEditor::EditorExportPlan planExport(Orkige::Project const& project,
	std::string const& platform)
{
	OrkigeEditor::EditorResourceLocator const& resources =
		OrkigeEditor::editorResources();
	OrkigeEditor::EditorExportInputs inputs;
	inputs.platform = platform;
	inputs.projectRoot = project.getRootDirectory();
	const Orkige::String nativeTarget =
		Orkige::NativeModule::configFromProject(project).target;
	inputs.nativeModule = !nativeTarget.empty();
	if (inputs.nativeModule)
	{
		// compiled game code needs an engine to build against - this editor's
		// build tree, or the SDK pack installed beside a downloaded app. ONE
		// resolution serves compile-on-Play and export (@see EditorEngineSdk.h),
		// so both refuse for the same reason in the same words.
		const OrkigeEditor::EngineSdkStatus sdk =
			OrkigeEditor::resolveEngineSdk(project.getName(), nativeTarget);
		inputs.nativeProblem = sdk.problem;
		inputs.sdkPack = sdk.engine.fromPack() ? sdk.engine.root
			: Orkige::String();
		inputs.moduleCmake = sdk.toolchain.cmake;
		inputs.moduleMakeProgram = sdk.toolchain.makeProgram;
	}
	// "is the tree this editor was built in still here?" - a configured build
	// tree is what the exporter packages from, so its cache is the probe
	std::error_code treeIgnored;
	inputs.engineTree = std::filesystem::exists(
		std::filesystem::path(ORKIGE_EDITOR_ENGINE_BUILD_DIR) /
		"CMakeCache.txt", treeIgnored);
	inputs.engineRoot = ORKIGE_EDITOR_ENGINE_ROOT;
	inputs.engineBuildDir = ORKIGE_EDITOR_ENGINE_BUILD_DIR;
	inputs.iosDeviceTree = ORKIGE_EDITOR_IOS_DEVICE_TREE;
	inputs.bundleResources = resources.bundleResourceRoot();
	inputs.bundleTools = resources.bundleToolRoot();
	inputs.bundlePlayer = resources.player().fromBundle();
	inputs.bundleMedia = resources.engineMedia().fromBundle();
	// the browser payload: a web build compiles nothing, so a copied app that
	// staged the wasm player can package for the browser on any host
	inputs.bundleWebPlayer = resources.webPlayer().fromBundle();
	// a DEVICE target's player is fetched rather than carried: ask the one
	// download seam whether this platform's is installed, and take its
	// ready-made sentence when it is not (@see EditorPayloads.h)
	const Orkige::String payloadId =
		OrkigeEditor::payloadIdForExportPlatform(platform);
	OrkigeEditor::FetchablePayload payload;
	if (!payloadId.empty() &&
		OrkigeEditor::findFetchablePayload(payloadId, payload))
	{
		inputs.devicePayload =
			OrkigeEditor::resolveInstalledPayload(payloadId, gEditorPayloads);
		if (inputs.devicePayload.empty())
		{
			inputs.devicePayloadProblem = OrkigeEditor::payloadMissingMessage(
				payload,
				OrkigeEditor::isPayloadEnabled(
					OrkigeEditor::parseEnabledPayloads(
						gViewSettings != nullptr ? gViewSettings->buildTargets
							: std::string()),
					payloadId),
				gEditorPayloads != nullptr);
		}
	}
	// the one file an export needs that is neither code nor engine media
	inputs.defaultIcon = resources.defaultAppIcon().path;
	inputs.hostPlatform = OrkigeEditor::hostExportPlatform();
	inputs.hostName = OrkigeEditor::hostExportName();
	return OrkigeEditor::planProjectExport(inputs);
}

//! @brief the exporter's view of the open project. Deliberately NOT the live
//! `Project`: loading one makes its AssetDatabase the process-wide active one,
//! which would rip the editor's out from under it (@see ExportProject.h). The
//! four manifest facts an export needs are copied out of the project the editor
//! already has open, so nothing is re-read and nothing global moves.
OrkigeExport::ExportProject exportProjectFor(Orkige::Project const& project)
{
	OrkigeExport::ExportProject out;
	out.root = project.getRootDirectory();
	out.name = project.getName();
	out.mainScene = project.getMainScene();
	out.settings = project.getSettings();
	return out;
}

//! @brief run one planned export to completion on the CALLING thread (the
//! worker body both async export drivers share - the Build menu's job below and
//! the control endpoint's `export_project`).
//!
//! The plan's two sourcing shapes map straight onto the exporter's request: a
//! Tree plan hands over its build tree AND the source tree beside it, a Bundle
//! plan hands over the staged payload roots and NO repository at all. That is
//! the whole "an exporter resolves files beside itself" rule - one field names
//! the engine source, and a plan can only fill one of the two shapes.
bool runPlannedExport(OrkigeEditor::EditorExportPlan const& plan,
	OrkigeExport::ExportProject const& project,
	std::function<void(std::string const&)> const& log,
	std::string& artifact, std::string& error)
{
	if (!plan.ok)
	{
		error = plan.error;
		return false;
	}
	OrkigeExport::ExportRequest request;
	request.platform = plan.platform;
	request.defaultIconPath = plan.defaultIcon;
	request.environment = OrkigeExport::currentEnvironment();
	if (plan.source == OrkigeEditor::EditorExportSource::Tree)
	{
		request.source.buildDirectory = plan.engineBuild;
		request.repoRoot = plan.repoRoot;
	}
	else
	{
		request.source.bundleResources = plan.bundleResources;
		request.source.bundleTools = plan.bundleTools;
		// compiled game code in a distributed shape: the engine to build it
		// against is the installed SDK pack, which is the pack's whole purpose
		request.source.sdkPack = plan.sdkPack;
		// a phone's player: fetched, kept outside the (signed) app, handed in
		// as a directory the exporter reads like any other engine source
		request.source.devicePayload = plan.devicePayload;
		request.cmake = plan.moduleCmake;
		request.ninja = plan.moduleMakeProgram;
	}
	return OrkigeExport::runExport(project, request, log, artifact, &error);
}

//! @brief start the export for the open project (async; false when it cannot
//! start). What it packages from is the plan's business: a preset build tree
//! per platform in the source-tree shape (the exporter reports honestly when
//! one of those was never built), the app's own staged payload in a distributed
//! copy.
bool startExport(ExportJob& job, Orkige::Project const& project,
	std::string const& platform, EditorConsole& console)
{
	if (job.isActive())
	{
		console.addLine(ConsoleLevel::Warning,
			"[export] an export is already running - wait for it to finish");
		return false;
	}
	if (!project.isLoaded())
	{
		return false; // the menu items are disabled without a project
	}
	const OrkigeEditor::EditorExportPlan plan = planExport(project, platform);
	if (!plan.ok)
	{
		console.addLine(ConsoleLevel::Error, "[export] " + plan.error);
		return false;
	}
	// the manifest facts are read HERE, on the main thread: Project is not
	// thread-safe and the worker must not touch it
	const OrkigeExport::ExportProject exportProject = exportProjectFor(project);
	job.reset();
	job.platform = platform;
	job.running.store(true);
	ExportJob* jobPointer = &job;
	// the worker touches NO SDL (the exporter's platform-tool seam spawns
	// through the C library), so it needs no SDL_CleanupTLS guard - the one
	// every SDL-calling Orkige worker carries
	job.worker = std::thread([jobPointer, plan, exportProject]()
	{
		std::string artifact;
		std::string error;
		auto log = [jobPointer](std::string const& line)
		{
			std::lock_guard<std::mutex> lock(jobPointer->mutex);
			jobPointer->lines.push_back(line);
		};
		const bool ok = runPlannedExport(plan, exportProject, log, artifact,
			error);
		std::lock_guard<std::mutex> lock(jobPointer->mutex);
		jobPointer->succeeded = ok;
		jobPointer->artifactPath = artifact;
		jobPointer->error = error;
		jobPointer->finished.store(true);
	});
	// stays on SDL_Log: a Console command-echo streamed under the "[export]"
	// prefix (the "[build]" precedent in EditorPlaySession), not an operational
	// diagnostic - the sink's [tag] prefix would break the bracket-prefix
	// contract Console readers key on
	SDL_Log("[export] packaging '%s' for %s...", project.getName().c_str(),
		platform.c_str());
	return true;
}

//! @brief per-frame pump: stream the exporter's progress into the Console as
//! "[export]" lines and, once the worker is done, report success (revealing the
//! artifact in Finder) or failure honestly
void updateExportJob(ExportJob& job, EditorConsole& console)
{
	if (!job.isActive())
	{
		return;
	}
	auto emitLine = [&console](std::string const& text)
	{
		ConsoleLevel level = ConsoleLevel::Info;
		if (text.find("ERROR") != std::string::npos ||
			text.find("error") != std::string::npos ||
			text.find("FAILED") != std::string::npos)
		{
			level = ConsoleLevel::Error;
		}
		else if (text.find("WARNING") != std::string::npos ||
			text.find("warning") != std::string::npos)
		{
			level = ConsoleLevel::Warning;
		}
		console.addLine(level, "[export] " + text);
	};
	std::vector<std::string> pending;
	bool finished = false;
	{
		std::lock_guard<std::mutex> lock(job.mutex);
		pending.swap(job.lines);
		finished = job.finished.load();
	}
	for (std::string const& line : pending)
	{
		emitLine(line);
	}
	if (!finished)
	{
		return; // still exporting; the editor stays responsive
	}
	if (job.worker.joinable())
	{
		job.worker.join();
	}
	job.running.store(false);
	if (!job.succeeded)
	{
		console.addLine(ConsoleLevel::Error, "[export] " + job.platform +
			" export FAILED: " + job.error);
		return;
	}
	console.addLine(ConsoleLevel::Info, "[export] " + job.platform +
		" export succeeded: " + job.artifactPath);
	// Play-in-Browser continuation: hand the artifact to the frame loop
	// (it serves the directory + opens the default browser - see
	// EditorBrowserServe.cpp). No Finder reveal - the browser opens instead.
	if (job.deployBrowser && !job.artifactPath.empty())
	{
		job.browserArtifactReady = true;
		return;
	}
#ifdef __APPLE__
	// iOS-device deploy continuation (Play on a connected iPhone): install the
	// freshly signed .app and launch it. This is dependency-free (devicectl);
	// the game then runs standalone from its bundled scene. The editor opens NO
	// live debug link - a USB device shares neither the host filesystem nor its
	// loopback, and no dependency-free CLI forwards a debug-port TCP tunnel to
	// it, so hierarchy/inspector streaming and pause/step are unavailable on
	// hardware (the documented gap, Docs/ios-signing.md). The install is a
	// blocking devicectl call (seconds), acceptable for this explicit one-shot.
	if (!job.deployDeviceUdid.empty() && !job.artifactPath.empty())
	{
		console.addLine(ConsoleLevel::Info, "[deploy] installing on '" +
			job.deployDeviceLabel + "' (devicectl - this takes a moment)...");
		std::string bundleId;
		std::string error;
		if (!iosHardwareInstallApp(job.deployDeviceUdid, job.artifactPath,
			bundleId, error))
		{
			console.addLine(ConsoleLevel::Error, "[deploy] install FAILED: " +
				error);
			job.deployDeviceUdid.clear();
			job.deployDeviceLabel.clear();
			return;
		}
		if (bundleId.empty())
		{
			console.addLine(ConsoleLevel::Error, "[deploy] installed but "
				"devicectl reported no bundle id - cannot launch");
			job.deployDeviceUdid.clear();
			job.deployDeviceLabel.clear();
			return;
		}
		console.addLine(ConsoleLevel::Info, "[deploy] launching '" + bundleId +
			"' on '" + job.deployDeviceLabel + "'...");
		if (!iosHardwareLaunchApp(job.deployDeviceUdid, bundleId, error))
		{
			console.addLine(ConsoleLevel::Error, "[deploy] launch FAILED: " +
				error);
		}
		else
		{
			console.addLine(ConsoleLevel::Info, "[deploy] running on '" +
				job.deployDeviceLabel + "'. Live debug over USB is unavailable "
				"(no dependency-free debug-port tunnel to a device - see "
				"Docs/ios-signing.md); the game runs standalone.");
		}
		job.deployDeviceUdid.clear();
		job.deployDeviceLabel.clear();
		return; // a device deploy does not reveal the .app in Finder
	}
	if (!job.artifactPath.empty())
	{
		// Reveal in Finder (fire and forget)
		// TODO(linux): xdg-open on the artifact's parent directory would be
		// the equivalent - wire it when exports exist on Linux (the export
		// tests/targets are APPLE-gated today, see tests/CMakeLists.txt)
		const char* revealArgs[] =
			{ "open", "-R", job.artifactPath.c_str(), nullptr };
		if (SDL_Process* reveal = SDL_CreateProcess(revealArgs, false))
		{
			SDL_DestroyProcess(reveal);
		}
	}
#endif
}
