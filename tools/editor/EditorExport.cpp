/********************************************************************
	created:	Thursday 2026/07/09 at 12:00
	filename: 	EditorExport.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// EditorExport.cpp - the async project export job (Build menu ->
// Util/orkige_export.py), output streamed into the Console.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"
#include "EditorExportPlan.h"
#include "EditorResourcePaths.h"
#include "PythonToolchain.h"

#include <core_project/NativeModule.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

//! @brief the exporter command for this project + platform, or the refusal
//! (@see EditorExportPlan.h). Every path is resolved through the ONE resource
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
	inputs.nativeModule =
		!Orkige::NativeModule::configFromProject(project).target.empty();
	// "is the tree this editor was built in still here?" - a configured build
	// tree is what the exporter packages from, so its cache is the probe
	std::error_code treeIgnored;
	inputs.engineTree = std::filesystem::exists(
		std::filesystem::path(ORKIGE_EDITOR_ENGINE_BUILD_DIR) /
		"CMakeCache.txt", treeIgnored);
	// The exporter comes from the SAME place the engine payload will: an
	// exporter resolves files beside itself (the web shell template lives at
	// tools/player/web/ in the source tree), so a bundled copy running against
	// a build tree looks for them inside the app and finds nothing. A build
	// tree that is still there wins; only a copy with no tree behind it takes
	// the staged exporter, which is exactly when the bundle IS the payload.
	inputs.exporter = inputs.engineTree
		? resources.pythonToolFromTree("orkige_export.py")
		: resources.pythonTool("orkige_export.py");
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
	inputs.hostPlatform = OrkigeEditor::hostExportPlatform();
	inputs.hostName = OrkigeEditor::hostExportName();
	return OrkigeEditor::planProjectExport(inputs);
}

//! @brief launch the exporter for the open project (async; false when it
//! cannot start). What it packages from is the plan's business: a preset build
//! tree per platform in the source-tree shape (the exporter reports honestly
//! when one of those was never built), the app's own staged payload in a
//! distributed copy.
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
	// preflight the python3 toolchain (cached per run) - the exporter is a
	// python3 script; report the missing/too-old interpreter honestly instead of
	// letting the spawn fail opaquely
	const Orkige::PythonProbeResult& python = Orkige::probePythonToolchain();
	if (!python.ok)
	{
		console.addLine(ConsoleLevel::Error, "[export] " + python.error);
		return false;
	}
	std::vector<std::string> command = { python.executable };
	command.insert(command.end(), plan.arguments.begin(),
		plan.arguments.end());
	std::vector<const char*> args;
	std::string commandLine;
	args.reserve(command.size() + 1);
	for (std::string const& arg : command)
	{
		args.push_back(arg.c_str());
		commandLine += (commandLine.empty() ? "" : " ") + arg;
	}
	args.push_back(nullptr);
	SDL_PropertiesID spawnProperties = SDL_CreateProperties();
	SDL_SetPointerProperty(spawnProperties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
		const_cast<char**>(args.data()));
	SDL_SetNumberProperty(spawnProperties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER,
		SDL_PROCESS_STDIO_APP);
	SDL_SetBooleanProperty(spawnProperties,
		SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
	job.process = SDL_CreateProcessWithProperties(spawnProperties);
	SDL_DestroyProperties(spawnProperties);
	if (!job.process)
	{
		console.addLine(ConsoleLevel::Error, "[export] FAILED to run '" +
			commandLine + "': " + SDL_GetError());
		return false;
	}
	job.platform = platform;
	job.outputBuffer.clear();
	job.artifactPath.clear();
	job.deployBrowser = false;
	job.browserArtifactReady = false;
	// stays on SDL_Log: a Console command-echo streamed under the "[export]"
	// prefix (the "[build]" precedent in EditorPlaySession), not an operational
	// diagnostic - the sink's [tag] prefix would break the bracket-prefix
	// contract Console readers key on
	SDL_Log("[export] $ %s", commandLine.c_str());
	return true;
}

//! @brief per-frame pump: stream the exporter's output into the Console as
//! "[export]" lines, capture the artifact path, and on exit report success
//! (revealing the artifact in Finder) or failure honestly
void updateExportJob(ExportJob& job, EditorConsole& console)
{
	if (!job.isActive())
	{
		return;
	}
	auto emitLine = [&console, &job](std::string const& text)
	{
		const std::string okPrefix = "orkige_export: OK ";
		if (text.rfind(okPrefix, 0) == 0)
		{
			job.artifactPath = text.substr(okPrefix.size());
		}
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
	SDL_IOStream* output = SDL_GetProcessOutput(job.process);
	if (output)
	{
		char chunk[4096];
		size_t bytesRead = 0;
		while ((bytesRead = SDL_ReadIO(output, chunk, sizeof(chunk))) > 0)
		{
			job.outputBuffer.append(chunk, bytesRead);
		}
	}
	std::size_t lineStart = 0;
	std::size_t newline = std::string::npos;
	while ((newline = job.outputBuffer.find('\n', lineStart)) !=
		std::string::npos)
	{
		std::string line =
			job.outputBuffer.substr(lineStart, newline - lineStart);
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		emitLine(line);
		lineStart = newline + 1;
	}
	job.outputBuffer.erase(0, lineStart);
	int exitCode = 0;
	if (!SDL_WaitProcess(job.process, false, &exitCode))
	{
		return; // still exporting; the editor stays responsive
	}
	if (!job.outputBuffer.empty())
	{
		emitLine(job.outputBuffer); // drain an unterminated tail line
		job.outputBuffer.clear();
	}
	SDL_DestroyProcess(job.process);
	job.process = nullptr;
	if (exitCode != 0)
	{
		console.addLine(ConsoleLevel::Error, "[export] " + job.platform +
			" export FAILED (exit " + std::to_string(exitCode) +
			") - see the lines above");
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
