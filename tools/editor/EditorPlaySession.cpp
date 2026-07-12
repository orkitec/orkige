// EditorPlaySession.cpp - the editor's play mode: temp-scene spawn of the
// player (desktop/other-flavor/simulator/adb), the native-module
// compile-on-Play build queue, the debug-link pump and crash detection.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <core_debugnet/DebugServer.h>
#include <core_game/SceneSerializer.h>
#include <core_project/NativeModule.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <sstream>

//! parse exactly count whitespace-separated floats; false on any junk
bool parsePlayFloats(std::string const& text, float* out, int count)
{
	std::istringstream stream(text);
	for (int i = 0; i < count; ++i)
	{
		if (!(stream >> out[i]))
		{
			return false;
		}
	}
	return true;
}

//! format floats with round-trip precision (the wire format for values)
std::string formatPlayFloats(const float* values, int count)
{
	std::string out;
	for (int i = 0; i < count; ++i)
	{
		char buffer[64];
		std::snprintf(buffer, sizeof(buffer), "%.9g", values[i]);
		if (i > 0)
		{
			out += ' ';
		}
		out += buffer;
	}
	return out;
}

//! forget everything streamed by the (previous) player
void clearRemoteState(PlaySession& session)
{
	session.remoteScenePath.clear();
	session.helloReceived = false;
	session.hierarchyReceived = false;
	session.remoteLogSeen = false;
	session.remoteHierarchy.clear();
	session.remoteParents.clear();
	session.remoteActive.clear();
	session.scriptErrorIds.clear();
	// the hot-reload watcher re-baselines against the fresh session's scripts
	// AND its .oui layouts (scriptsWatchArmed arms both)
	session.scriptsWatchArmed = false;
	session.scriptsNewestMtime = 0;
	session.uiFileMtimes.clear();
	session.remoteSelectedId.clear();
	session.stateObjectId.clear();
	session.stateComponents.clear();
	session.stateProperties.clear();
	session.statePropKeys.clear();
	session.statePropKind.clear();
	session.statePropHint.clear();
	session.statePropReadonly.clear();
	session.lastScreenshotPath.clear();
	session.lastScreenshotError.clear();
	session.lastScreenshotOk = false;
	session.screenshotSeq = 0;
	session.recordingActive = false;
	session.lastRecordPath.clear();
	session.lastRecordError.clear();
	session.lastRecordOk = false;
	session.recordSeq = 0;
	session.remoteMemRss = -1;
	session.remoteMemRssPeak = -1;
	session.remoteWindowW = -1;
	session.remoteWindowH = -1;
	session.remoteSafeLeft = -1;
	session.remoteSafeTop = -1;
	session.remoteSafeRight = -1;
	session.remoteSafeBottom = -1;
	session.remoteUiLayout.clear();
	session.remoteScreenCurrent.clear();
	session.remoteScreenStack.clear();
	session.remoteAllocPerFrame = -1;
	session.remoteAllocPeak = -1;
	session.remoteAllocTags.clear();
	session.remoteAllocCounts.clear();
	session.remoteFrameMs = -1.0;
	session.remoteGameState.clear();
	session.remoteProfile.clear();
	session.remoteProfileFrameMs = -1.0;
	session.profileSeq = 0;
	session.remoteMusic.clear();
}

//! @brief tear the session down (reap/kill the player, drop the link,
//! delete the temp scene) and revert to edit mode - the single exit path
//! for Stop, crash detection and editor shutdown
void endPlaySession(PlaySession& session, std::string const& reason)
{
	if (session.buildProcess)
	{
		// a running native module build dies with the session (Stop while
		// building = cancel; a failed build also funnels through here)
		int buildExit = 0;
		if (!SDL_WaitProcess(session.buildProcess, false, &buildExit))
		{
			SDL_KillProcess(session.buildProcess, true);
			SDL_WaitProcess(session.buildProcess, true, &buildExit);
		}
		SDL_DestroyProcess(session.buildProcess);
		session.buildProcess = nullptr;
	}
	session.buildSteps.clear();
	session.buildOutputBuffer.clear();
	session.nativeExecutable.clear();
	session.nativeTarget.clear();
	if (session.process)
	{
		int exitCode = 0;
		if (!SDL_WaitProcess(session.process, false, &exitCode))
		{
			SDL_KillProcess(session.process, true);
			SDL_WaitProcess(session.process, true, &exitCode);
		}
		SDL_DestroyProcess(session.process);
		session.process = nullptr;
	}
	if (session.simPrepProcess)
	{
		// a still-running simctl boot/install is left to finish on its own
		// (killing simctl mid-boot can wedge CoreSimulator) - only the handle
		// is dropped; the session itself is over
		int prepExit = 0;
		if (!SDL_WaitProcess(session.simPrepProcess, false, &prepExit))
		{
			SDL_Log("orkige_editor: play - abandoning the running simulator "
				"preparation step (simctl finishes in the background)");
		}
		SDL_DestroyProcess(session.simPrepProcess);
		session.simPrepProcess = nullptr;
	}
	session.simPrep = PlaySession::SimPrep::None;
	session.launchStatus.clear();
#ifdef __APPLE__
	if (session.onSimulator)
	{
		// belt-and-braces: MSG_QUIT normally ends the app, but a hung or
		// disconnected player must not keep running on the device
		const char* terminateArgs[] = { "/usr/bin/xcrun", "simctl",
			"terminate", session.simulatorUdid.c_str(),
			PLAY_SIMULATOR_BUNDLE_ID, nullptr };
		std::string terminateOutput;
		int terminateExit = 0;
		runProcessCaptured(terminateArgs, terminateOutput, terminateExit);
	}
#endif
	if (session.onAndroid)
	{
		// belt-and-braces stop, mirroring the simulator teardown
		std::string ignoredOutput;
		int ignoredExit = 0;
		runProcessCaptured({ adbPath(), "-s", session.androidSerial, "shell",
			"am", "force-stop", PLAY_ANDROID_PACKAGE },
			ignoredOutput, ignoredExit);
	}
	if (session.androidForwarded)
	{
		const std::string portSpec = "tcp:" + std::to_string(session.port);
		std::string ignoredOutput;
		int ignoredExit = 0;
		runProcessCaptured({ adbPath(), "-s", session.androidSerial,
			"forward", "--remove", portSpec }, ignoredOutput, ignoredExit);
		session.androidForwarded = false;
	}
	session.onAndroid = false;
	session.onSimulator = false;
	session.client.disconnect();
	if (!session.tempScenePath.empty())
	{
		std::error_code ignored;
		std::filesystem::remove(session.tempScenePath, ignored);
		session.tempScenePath.clear();
	}
	clearRemoteState(session);
	session.projectRoot.clear();
	session.mode = PlaySession::Mode::Edit;
	SDL_Log("orkige_editor: play mode ended (%s)", reason.c_str());
}

namespace
{

#ifdef __APPLE__
//! @brief reap a finished simulator prep process (simctl boot/install);
//! false while it is still running. Output and exit code are only valid on
//! true; the process handle is destroyed.
bool reapSimPrepProcess(PlaySession& session, std::string& output,
	int& exitCode)
{
	if (!session.simPrepProcess ||
		!SDL_WaitProcess(session.simPrepProcess, false, &exitCode))
	{
		return false;
	}
	size_t outputSize = 0;
	void* data =
		SDL_ReadProcess(session.simPrepProcess, &outputSize, &exitCode);
	output.assign(data ? static_cast<char*>(data) : "", data ? outputSize : 0);
	SDL_free(data);
	SDL_DestroyProcess(session.simPrepProcess);
	session.simPrepProcess = nullptr;
	return true;
}

//! @brief launch the installed player app on the (booted) simulator - the
//! final SimPrep step; on success the normal connect loop takes over with a
//! fresh connect-timeout window
void launchPlayerOnSimulator(PlaySession& session)
{
	const std::string portString = std::to_string(session.port);
	// simulator apps read the host filesystem, so the project root travels
	// as a plain --project path exactly like on the desktop
	std::vector<std::string> launchArgs = { "/usr/bin/xcrun", "simctl",
		"launch", "--terminate-running-process", session.simulatorUdid,
		PLAY_SIMULATOR_BUNDLE_ID, session.tempScenePath,
		"--debug-port", portString };
	if (!session.projectRoot.empty())
	{
		launchArgs.push_back("--project");
		launchArgs.push_back(session.projectRoot);
	}
	std::string launchOutput;
	int launchExit = 0;
	if (!runProcessCaptured(launchArgs, launchOutput, launchExit) ||
		launchExit != 0)
	{
		SDL_Log("orkige_editor: play failed - simctl launch on '%s' (exit "
			"%d): %s", session.simulatorLabel.c_str(), launchExit,
			launchOutput.c_str());
		endPlaySession(session, "simctl launch failed");
		return;
	}
	session.simPrep = PlaySession::SimPrep::None;
	session.launchStatus = "launching player on '" + session.simulatorLabel +
		"'...";
	// the connect window starts NOW - boot/install time must not eat into it
	session.launchStart = std::chrono::steady_clock::now();
	session.lastConnectAttempt = std::chrono::steady_clock::time_point();
	SDL_Log("orkige_editor: play - launched player on simulator '%s' (scene "
		"'%s', port %u)", session.simulatorLabel.c_str(),
		session.tempScenePath.c_str(), static_cast<unsigned>(session.port));
}

//! @brief per-frame simulator preparation pump (mode == Launching with
//! simPrep != None): wait for boot -> poll until Booted -> app check (auto-
//! install from the ios-simulator-debug build when missing) -> launch. Every
//! failure and the hard timeout end the session with an honest Console line.
void advanceSimulatorPrep(PlaySession& session)
{
	const std::chrono::steady_clock::time_point now =
		std::chrono::steady_clock::now();
	if (now - session.simPrepStart >
		std::chrono::seconds(PLAY_SIM_PREP_TIMEOUT_SECONDS))
	{
		SDL_Log("orkige_editor: play failed - simulator '%s' did not become "
			"ready within %d seconds", session.simulatorLabel.c_str(),
			PLAY_SIM_PREP_TIMEOUT_SECONDS);
		endPlaySession(session, "simulator preparation timed out");
		return;
	}
	switch (session.simPrep)
	{
	case PlaySession::SimPrep::WaitBootProcess:
	{
		std::string output;
		int exitCode = 0;
		if (!reapSimPrepProcess(session, output, exitCode))
		{
			return; // simctl boot still running
		}
		// 'simctl boot' on an already-booted device exits 149 ("current
		// state: Booted") - a race with a manually started boot, not an error
		if (exitCode != 0 &&
			output.find("current state: Booted") == std::string::npos)
		{
			SDL_Log("orkige_editor: play failed - 'simctl boot %s' exited "
				"with %d: %s", session.simulatorUdid.c_str(), exitCode,
				output.c_str());
			endPlaySession(session, "simctl boot failed");
			return;
		}
		session.simPrep = PlaySession::SimPrep::WaitBooted;
		session.launchStatus = "waiting for simulator '" +
			session.simulatorLabel + "' to finish booting...";
		return;
	}
	case PlaySession::SimPrep::WaitBooted:
	{
		// poll the device state once a second (simctl list is not free)
		if (session.simLastPoll != std::chrono::steady_clock::time_point() &&
			now - session.simLastPoll < std::chrono::milliseconds(1000))
		{
			return;
		}
		session.simLastPoll = now;
		if (!simulatorIsBooted(session.simulatorUdid))
		{
			return; // still coming up
		}
		// booted - is a CURRENT player app on it? (a stale install is
		// replaced, not launched - see simulatorPlayerUpToDate)
		if (simulatorPlayerUpToDate(session.simulatorUdid))
		{
			launchPlayerOnSimulator(session);
			return;
		}
		std::error_code ignored;
		if (!std::filesystem::exists(ORKIGE_EDITOR_IOS_PLAYER_APP, ignored))
		{
			SDL_Log("orkige_editor: play failed - OrkigePlayer.app is "
				"neither installed on '%s' nor built at '%s'. Build the iOS "
				"player first: cmake --preset ios-simulator-debug && cmake "
				"--build --preset ios-simulator-debug",
				session.simulatorLabel.c_str(), ORKIGE_EDITOR_IOS_PLAYER_APP);
			endPlaySession(session, "player app not available");
			return;
		}
		const char* installArgs[] = { "/usr/bin/xcrun", "simctl", "install",
			session.simulatorUdid.c_str(), ORKIGE_EDITOR_IOS_PLAYER_APP,
			nullptr };
		session.simPrepProcess = SDL_CreateProcess(installArgs, true);
		if (!session.simPrepProcess)
		{
			SDL_Log("orkige_editor: play failed - could not run 'simctl "
				"install': %s", SDL_GetError());
			endPlaySession(session, "simctl install spawn failed");
			return;
		}
		session.simPrep = PlaySession::SimPrep::Installing;
		session.launchStatus = "installing player on '" +
			session.simulatorLabel + "'...";
		SDL_Log("orkige_editor: play - installing '%s' on '%s'",
			ORKIGE_EDITOR_IOS_PLAYER_APP, session.simulatorLabel.c_str());
		return;
	}
	case PlaySession::SimPrep::Installing:
	{
		std::string output;
		int exitCode = 0;
		if (!reapSimPrepProcess(session, output, exitCode))
		{
			return; // simctl install still running
		}
		if (exitCode != 0)
		{
			SDL_Log("orkige_editor: play failed - 'simctl install' exited "
				"with %d: %s", exitCode, output.c_str());
			endPlaySession(session, "simctl install failed");
			return;
		}
		SDL_Log("orkige_editor: play - player installed on '%s'",
			session.simulatorLabel.c_str());
		launchPlayerOnSimulator(session);
		return;
	}
	case PlaySession::SimPrep::None:
		return;
	}
}
#endif // __APPLE__

//! @brief spawn a desktop play process (the generic player or a project's
//! built native module executable) on the session's temp scene + debug port
//! and enter Launching. The automation env hooks aimed at THIS editor
//! process (frame caps, screenshot paths) must not leak into the spawned
//! process - it honours the same variables and would e.g. exit after N
//! frames or overwrite the requested screenshot.
bool spawnDesktopPlayProcess(PlaySession& session,
	std::string const& executablePath)
{
	const std::string portString = std::to_string(session.port);
	std::vector<const char*> args = { executablePath.c_str(),
		session.tempScenePath.c_str(), "--debug-port", portString.c_str() };
	if (!session.projectRoot.empty())
	{
		// with a project open the process plays IN that project: its assets/
		// and scenes/ register as resource locations over there too
		args.push_back("--project");
		args.push_back(session.projectRoot.c_str());
	}
	args.push_back(nullptr);
	SDL_Environment* playerEnvironment = SDL_CreateEnvironment(true);
	SDL_UnsetEnvironmentVariable(playerEnvironment, "ORKIGE_DEMO_FRAMES");
	SDL_UnsetEnvironmentVariable(playerEnvironment, "ORKIGE_DEMO_SCREENSHOT");
	SDL_PropertiesID spawnProperties = SDL_CreateProperties();
	SDL_SetPointerProperty(spawnProperties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
		const_cast<char**>(args.data()));
	SDL_SetPointerProperty(spawnProperties,
		SDL_PROP_PROCESS_CREATE_ENVIRONMENT_POINTER, playerEnvironment);
	session.process = SDL_CreateProcessWithProperties(spawnProperties);
	SDL_DestroyProperties(spawnProperties);
	SDL_DestroyEnvironment(playerEnvironment);
	if (!session.process)
	{
		SDL_Log("orkige_editor: play failed - SDL_CreateProcess '%s': %s",
			executablePath.c_str(), SDL_GetError());
		endPlaySession(session, "spawn failed");
		return false;
	}
	clearRemoteState(session);
	session.mode = PlaySession::Mode::Launching;
	session.launchStatus.clear();
	session.launchStart = std::chrono::steady_clock::now();
	session.lastConnectAttempt = std::chrono::steady_clock::time_point();
	SDL_Log("orkige_editor: play - spawned '%s' (scene '%s', port %u)",
		executablePath.c_str(), session.tempScenePath.c_str(),
		static_cast<unsigned>(session.port));
	return true;
}

//! @brief start the next queued native-module build step (mode == Building):
//! stdout+stderr are piped so updatePlaySession streams them into the
//! Console as "[build]" lines. False (with the session ended) when the
//! process cannot be spawned.
bool startNextBuildStep(PlaySession& session)
{
	if (session.buildSteps.empty())
	{
		return false;
	}
	const std::vector<std::string> step = session.buildSteps.front();
	session.buildSteps.erase(session.buildSteps.begin());
	std::vector<const char*> args;
	std::string commandLine;
	args.reserve(step.size() + 1);
	for (std::string const& arg : step)
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
	session.buildProcess = SDL_CreateProcessWithProperties(spawnProperties);
	SDL_DestroyProperties(spawnProperties);
	if (!session.buildProcess)
	{
		SDL_Log("[build] FAILED to run '%s': %s", commandLine.c_str(),
			SDL_GetError());
		endPlaySession(session, "native build spawn failed");
		return false;
	}
	// the SDL log hook mirrors this into the Console as a "[build]" line
	SDL_Log("[build] $ %s", commandLine.c_str());
	return true;
}

//! @brief drain whatever the running build step wrote (non-blocking pipe)
//! into the Console, one "[build]"-tagged line each; compiler diagnostics
//! colour as errors/warnings so a failed Play is impossible to miss
void pumpBuildOutput(PlaySession& session, EditorConsole& console,
	bool flushPartialLine)
{
	SDL_IOStream* output = session.buildProcess
		? SDL_GetProcessOutput(session.buildProcess) : nullptr;
	if (output)
	{
		char chunk[4096];
		size_t bytesRead = 0;
		while ((bytesRead = SDL_ReadIO(output, chunk, sizeof(chunk))) > 0)
		{
			session.buildOutputBuffer.append(chunk, bytesRead);
		}
	}
	std::size_t lineStart = 0;
	std::size_t newline = std::string::npos;
	auto emitLine = [&console, &session](std::string const& text)
	{
		ConsoleLevel level = ConsoleLevel::Info;
		if (text.find("error") != std::string::npos ||
			text.find("FAILED") != std::string::npos)
		{
			level = ConsoleLevel::Error;
		}
		else if (text.find("warning") != std::string::npos)
		{
			level = ConsoleLevel::Warning;
		}
		console.addLine(level, "[build] " + text);
		// keep the error lines for the control port's structured build_errors -
		// the compiler diagnostics an agent needs to fix a failed compile-on-Play
		if (level == ConsoleLevel::Error)
		{
			session.buildErrorLog += text;
			session.buildErrorLog += '\n';
			// cap the tail so a runaway build cannot grow it without bound
			const std::size_t maxErrorLog = 8192;
			if (session.buildErrorLog.size() > maxErrorLog)
			{
				session.buildErrorLog.erase(0,
					session.buildErrorLog.size() - maxErrorLog);
			}
		}
	};
	while ((newline = session.buildOutputBuffer.find('\n', lineStart)) !=
		std::string::npos)
	{
		std::string line =
			session.buildOutputBuffer.substr(lineStart, newline - lineStart);
		if (!line.empty() && line.back() == '\r')
		{
			line.pop_back();
		}
		emitLine(line);
		lineStart = newline + 1;
	}
	session.buildOutputBuffer.erase(0, lineStart);
	if (flushPartialLine && !session.buildOutputBuffer.empty())
	{
		emitLine(session.buildOutputBuffer);
		session.buildOutputBuffer.clear();
	}
}

} // namespace

//! @brief Play: save the current scene to a temp play file, spawn the
//! player with --debug-port on a probed free port and start connecting.
//! The user's scene file and the editor world stay untouched.
//! @param project the open project (unloaded = loose-scene mode). Its root
//! is forwarded to the play process as --project so the project's resource
//! roots (and its scripts) resolve identically in the runtime; a project
//! with native module settings switches Play to compile-on-Play (build the
//! module, then run ITS executable instead of the generic player).
bool startPlay(PlaySession& session,
	Orkige::GameObjectManager& gameObjectManager,
	Orkige::Project const& project)
{
	if (session.isActive())
	{
		return false;
	}
	// a fresh run starts with no build verdict; the native branch below flips
	// it to Building. clearRemoteState/endPlaySession leave these alone so the
	// outcome outlives the session for the control port's get_state.
	session.buildOutcome = PlaySession::BuildOutcome::None;
	session.buildStatusTarget.clear();
	session.buildErrorLog.clear();
	const std::string projectRoot = project.getRootDirectory();
	const Orkige::NativeModule::Config nativeConfig = project.isLoaded()
		? Orkige::NativeModule::configFromProject(project)
		: Orkige::NativeModule::Config();
	if (nativeConfig.enabled &&
		(!session.simulatorUdid.empty() || !session.androidSerial.empty()))
	{
		// honest refusal instead of silently playing the wrong binary: the
		// module was built for the desktop host - device builds of native
		// modules are the export milestone's job
		SDL_Log("orkige_editor: play refused - project '%s' has a native "
			"module ('%s'), which can only play on the Desktop target for "
			"now (device targets need the export pipeline)",
			project.getName().c_str(), nativeConfig.target.c_str());
		return false;
	}
	if (nativeConfig.enabled && !session.desktopPlayerPath.empty())
	{
		// same honesty for the cross-flavor desktop target: the module
		// compiles and links against THIS editor's build tree - it cannot
		// play on the other render flavor's runtime
		SDL_Log("orkige_editor: play refused - project '%s' has a native "
			"module ('%s'), which plays its own executable built against "
			"this editor's render flavor (pick the plain Desktop target)",
			project.getName().c_str(), nativeConfig.target.c_str());
		return false;
	}
	session.projectRoot = projectRoot;
	// temp play file (never the user's file - saveScene is called directly,
	// EditorState::currentScenePath/sceneDirty are not involved)
	const std::string tempName = "orkige_play_" + std::to_string(
		std::chrono::steady_clock::now().time_since_epoch().count()) +
		".oscene";
	session.tempScenePath =
		(std::filesystem::temp_directory_path() / tempName).string();
	if (!Orkige::SceneSerializer::saveScene(session.tempScenePath,
		gameObjectManager))
	{
		SDL_Log("orkige_editor: play failed - could not save temp scene '%s'",
			session.tempScenePath.c_str());
		session.tempScenePath.clear();
		return false;
	}
	// free localhost port: bind an ephemeral DebugServer, read the port
	// back, close it again (tiny race until the player re-binds it -
	// acceptable for a local dev loop)
	{
		Orkige::DebugServer portProbe;
		if (!portProbe.start(0))
		{
			SDL_Log("orkige_editor: play failed - no free debug port");
			endPlaySession(session, "port probe failed");
			return false;
		}
		session.port = portProbe.getPort();
	}
	if (nativeConfig.enabled)
	{
		// compile-on-Play: queue configure (only when the build tree has no
		// cache yet) + incremental build of the project's native module and
		// enter Building; updatePlaySession pumps the output into the
		// Console and launches the module executable on success. All paths
		// (cmake, engine build tree, toolchain settings) are this editor
		// build's own constants - the engine libs the module links are the
		// ones this very editor runs on.
		const std::string moduleSourceDir =
			project.resolvePath(nativeConfig.cmakeDir);
		const std::string moduleBuildDir =
			project.resolvePath(nativeConfig.buildDir);
		std::error_code ignored;
		if (!std::filesystem::exists(
			std::filesystem::path(moduleSourceDir) / "CMakeLists.txt", ignored))
		{
			SDL_Log("orkige_editor: play failed - project '%s' declares "
				"native module '%s' but '%s' has no CMakeLists.txt",
				project.getName().c_str(), nativeConfig.target.c_str(),
				moduleSourceDir.c_str());
			endPlaySession(session, "native module misconfigured");
			return false;
		}
		Orkige::StringVector extraArguments = {
			std::string("-DCMAKE_MAKE_PROGRAM=") + ORKIGE_EDITOR_MAKE_PROGRAM,
			std::string("-DORKIGE_SCRIPTING=") + ORKIGE_EDITOR_SCRIPTING,
			// hermeticity, same as the presets: never let the module build
			// pick up the banned /usr/local prefix
			"-DCMAKE_IGNORE_PREFIX_PATH=/usr/local",
		};
#ifdef __APPLE__
		if (ORKIGE_EDITOR_OSX_SYSROOT[0] != '\0')
		{
			extraArguments.push_back(std::string("-DCMAKE_OSX_SYSROOT=") +
				ORKIGE_EDITOR_OSX_SYSROOT);
		}
#endif
		session.buildSteps.clear();
		if (Orkige::NativeModule::needsConfigure(moduleBuildDir))
		{
			session.buildSteps.push_back(
				Orkige::NativeModule::configureCommand(ORKIGE_EDITOR_CMAKE,
					moduleSourceDir, moduleBuildDir,
					ORKIGE_EDITOR_ENGINE_ROOT, ORKIGE_EDITOR_ENGINE_BUILD_DIR,
					ORKIGE_EDITOR_BUILD_TYPE, extraArguments));
		}
		session.buildSteps.push_back(Orkige::NativeModule::buildCommand(
			ORKIGE_EDITOR_CMAKE, moduleBuildDir));
		session.nativeExecutable = Orkige::NativeModule::executablePath(
			moduleBuildDir, nativeConfig.target);
		session.nativeTarget = nativeConfig.target;
		clearRemoteState(session);
		session.mode = PlaySession::Mode::Building;
		session.buildOutcome = PlaySession::BuildOutcome::Building;
		session.buildStatusTarget = nativeConfig.target;
		session.launchStatus = "building native module '" +
			nativeConfig.target + "'...";
		SDL_Log("[build] building native module '%s' of project '%s' "
			"(build dir '%s')", nativeConfig.target.c_str(),
			project.getName().c_str(), moduleBuildDir.c_str());
		return startNextBuildStep(session);
	}
	if (!session.androidSerial.empty())
	{
		// Play on a connected Android device/emulator: unlike the iOS
		// simulator, the device shares neither the filesystem nor the
		// loopback with the host. The temp scene is dropped into the app
		// files dir (adb push to /data/local/tmp + run-as copy - the
		// standard debuggable-app file drop) and the debug link rides an
		// 'adb forward tcp' bridge: the player LISTENS on the device
		// loopback, the editor keeps connecting to 127.0.0.1 on the host.
		// Physical Android phones deploy through this identical flow.
		const std::string adb = adbPath();
		const std::string portString = std::to_string(session.port);
		const std::string portSpec = "tcp:" + portString;
		const std::string devicePushPath =
			std::string("/data/local/tmp/") + PLAY_ANDROID_SCENE_NAME;
		const std::string deviceScenePath =
			std::string("files/") + PLAY_ANDROID_SCENE_NAME;
		const std::vector<std::vector<std::string>> steps = {
			// a fresh process (and thus a fresh SDL_main) per session
			{ adb, "-s", session.androidSerial, "shell",
				"am", "force-stop", PLAY_ANDROID_PACKAGE },
			{ adb, "-s", session.androidSerial, "push",
				session.tempScenePath, devicePushPath },
			{ adb, "-s", session.androidSerial, "shell",
				"run-as", PLAY_ANDROID_PACKAGE, "mkdir", "-p", "files" },
			{ adb, "-s", session.androidSerial, "shell",
				"run-as", PLAY_ANDROID_PACKAGE, "cp",
				devicePushPath, deviceScenePath },
			{ adb, "-s", session.androidSerial,
				"forward", portSpec, portSpec },
			// scene + port travel as intent extras; OrkigeActivity turns
			// them into the SDL_main argv (the relative scene path is
			// resolved against the app files dir by the player)
			{ adb, "-s", session.androidSerial, "shell",
				"am", "start", "-n", PLAY_ANDROID_ACTIVITY,
				"--es", "scene", PLAY_ANDROID_SCENE_NAME,
				"--ei", "debugPort", portString },
		};
		for (std::vector<std::string> const& step : steps)
		{
			std::string output;
			int exitCode = 0;
			if (!runProcessCaptured(step, output, exitCode) || exitCode != 0)
			{
				SDL_Log("orkige_editor: play failed - adb step '%s' on '%s' "
					"(exit %d): %s (is the player APK installed? see "
					"tools/player/android/package_apk.sh)",
					step[3].c_str(), session.androidLabel.c_str(), exitCode,
					output.c_str());
				endPlaySession(session, "adb deploy failed");
				return false;
			}
			if (step[3] == "forward")
			{
				session.androidForwarded = true;
			}
		}
		session.onAndroid = true;
		clearRemoteState(session);
		session.mode = PlaySession::Mode::Launching;
		session.launchStart = std::chrono::steady_clock::now();
		session.lastConnectAttempt = std::chrono::steady_clock::time_point();
		SDL_Log("orkige_editor: play - launched player on Android '%s' "
			"(scene '%s', forwarded port %u)", session.androidLabel.c_str(),
			session.tempScenePath.c_str(), static_cast<unsigned>(session.port));
		return true;
	}
#ifdef __APPLE__
	if (!session.simulatorUdid.empty())
	{
		// Play on an iOS simulator. A booted one goes straight to the app
		// check + launch; a SHUTDOWN one is booted first ('simctl boot' is
		// headless, so Simulator.app is opened alongside to bring the device
		// window up), then checked/auto-installed, then launched - all
		// asynchronously through the SimPrep pipeline (advanceSimulatorPrep)
		// with live status text in the toolbar, never a silent no-op.
		// Simulator apps read the host filesystem and share the host
		// loopback, so the temp scene path and the 127.0.0.1 debug link work
		// unchanged. simctl launch returns as soon as the app process starts,
		// so session.process stays null and liveness is tracked purely
		// through the debug link.
		const bool booted = simulatorIsBooted(session.simulatorUdid);
		session.onSimulator = true;
		clearRemoteState(session);
		session.mode = PlaySession::Mode::Launching;
		session.launchStart = std::chrono::steady_clock::now();
		session.lastConnectAttempt = std::chrono::steady_clock::time_point();
		session.simPrepStart = session.launchStart;
		session.simLastPoll = std::chrono::steady_clock::time_point();
		session.simulatorBootedByEditor = false;
		// bring the Simulator UI up in every case: 'simctl boot' alone is
		// headless, and even a Booted device shows no window when
		// Simulator.app was quit - "Play didn't open the simulator" must not
		// happen again. A failed open is logged and the run continues
		// headless (the player still works, only the window is missing).
		{
			const char* openArgs[] = { "/usr/bin/open", "-a", "Simulator",
				"--args", "-CurrentDeviceUDID",
				session.simulatorUdid.c_str(), nullptr };
			std::string openOutput;
			int openExit = 0;
			if (!runProcessCaptured(openArgs, openOutput, openExit) ||
				openExit != 0)
			{
				SDL_Log("orkige_editor: play - 'open -a Simulator' failed "
					"(exit %d): %s - continuing headless", openExit,
					openOutput.c_str());
			}
		}
		if (!booted)
		{
			const char* bootArgs[] = { "/usr/bin/xcrun", "simctl", "boot",
				session.simulatorUdid.c_str(), nullptr };
			session.simPrepProcess = SDL_CreateProcess(bootArgs, true);
			if (!session.simPrepProcess)
			{
				SDL_Log("orkige_editor: play failed - could not run 'simctl "
					"boot' for '%s': %s", session.simulatorLabel.c_str(),
					SDL_GetError());
				endPlaySession(session, "simctl boot spawn failed");
				return false;
			}
			session.simulatorBootedByEditor = true;
			session.simPrep = PlaySession::SimPrep::WaitBootProcess;
			session.launchStatus = "booting simulator '" +
				session.simulatorLabel + "'...";
			SDL_Log("orkige_editor: play - booting shutdown simulator '%s' "
				"(%s)", session.simulatorLabel.c_str(),
				session.simulatorUdid.c_str());
		}
		else
		{
			session.simPrep = PlaySession::SimPrep::WaitBooted;
			session.launchStatus = "checking player on '" +
				session.simulatorLabel + "'...";
		}
		return true;
	}
#endif
	// spawn the generic player: this build's own (ORKIGE_EDITOR_PLAYER_PATH,
	// baked in by CMake as $<TARGET_FILE:orkige_player>) or the OTHER render
	// flavor's binary when the toolbar picked it (the debug protocol is
	// flavor-agnostic). SDL's process API keeps the editor free of platform
	// spawn code; stdio stays inherited so the player log shows up in the
	// editor console.
	return spawnDesktopPlayProcess(session, session.desktopPlayerPath.empty()
		? ORKIGE_EDITOR_PLAYER_PATH : session.desktopPlayerPath);
}

//! Stop: ask the player to quit; updatePlaySession reaps it (or kills it
//! after the grace timeout)
void requestStopPlay(PlaySession& session)
{
	if (!session.isActive() || session.mode == PlaySession::Mode::Stopping)
	{
		return;
	}
	if (session.mode == PlaySession::Mode::Building)
	{
		// Stop while the native module compiles = cancel the build outright
		// (endPlaySession kills the cmake process); nothing was launched yet
		endPlaySession(session, "native build canceled");
		return;
	}
	if (session.client.isConnected())
	{
		session.client.send(Orkige::DebugMessage(Protocol::MSG_QUIT));
	}
	session.mode = PlaySession::Mode::Stopping;
	session.stopRequestTime = std::chrono::steady_clock::now();
	SDL_Log("orkige_editor: play - stop requested");
}

//! remote selection: remember it and tell the player what to stream
void selectRemoteObject(PlaySession& session, std::string const& id)
{
	session.remoteSelectedId = id;
	Orkige::DebugMessage select(Protocol::MSG_SELECT);
	select.set(Protocol::FIELD_ID, id);
	session.client.send(select);
}

//! remote active toggle (the play-mode Hierarchy/Inspector checkbox): the
//! player applies GameObject::setActive and streams a fresh hierarchy back
void setRemoteObjectActive(PlaySession& session, std::string const& id,
	bool active)
{
	Orkige::DebugMessage setActive(Protocol::MSG_SET_ACTIVE);
	setActive.set(Protocol::FIELD_ID, id);
	setActive.set(Protocol::FIELD_VALUE, active ? "1" : "0");
	session.client.send(setActive);
}

//! live property write on the running game (reflected setter on the player)
void setRemoteObjectProperty(PlaySession& session, std::string const& id,
	std::string const& component, std::string const& property,
	std::string const& value)
{
	if (!session.client.isConnected())
	{
		return;
	}
	Orkige::DebugMessage set(Protocol::MSG_SET_PROPERTY);
	set.set(Protocol::FIELD_ID, id);
	set.set(Protocol::FIELD_COMPONENT, component);
	set.set(Protocol::FIELD_PROPERTY, property);
	set.set(Protocol::FIELD_VALUE, value);
	session.client.send(set);
}

//! live cvar tuning on the running game
void setRemoteCvar(PlaySession& session, std::string const& name,
	std::string const& value)
{
	if (!session.client.isConnected())
	{
		return;
	}
	Orkige::DebugMessage cvar(Protocol::MSG_SET_CVAR);
	cvar.set(Protocol::FIELD_CVAR_NAME, name);
	cvar.set(Protocol::FIELD_VALUE, value);
	session.client.send(cvar);
}

//! ask the running game to capture its next frame to path
void requestRemoteScreenshot(PlaySession& session, std::string const& path)
{
	if (!session.client.isConnected())
	{
		return;
	}
	Orkige::DebugMessage shot(Protocol::MSG_SCREENSHOT);
	shot.set(Protocol::FIELD_PATH, path);
	session.client.send(shot);
}

//! ask the running game to record a .jsonl flight-recorder trace
void requestRemoteRecord(PlaySession& session, std::string const& path,
	float maxSeconds, unsigned int everyNth, std::string const& objects)
{
	if (!session.client.isConnected())
	{
		return;
	}
	Orkige::DebugMessage record(Protocol::MSG_RECORD_START);
	record.set(Protocol::FIELD_PATH, path);
	record.setFloat(Protocol::FIELD_SECONDS, maxSeconds);
	record.setFloat(Protocol::FIELD_EVERY,
		static_cast<float>(everyNth == 0 ? 1u : everyNth));
	if (!objects.empty())
	{
		record.set(Protocol::FIELD_FILTER, objects);
	}
	session.client.send(record);
	session.recordingActive = true;
}

//! ask the running game to stop an in-progress trace early
void stopRemoteRecord(PlaySession& session)
{
	if (!session.client.isConnected())
	{
		return;
	}
	Orkige::DebugMessage stop(Protocol::MSG_RECORD_STOP);
	session.client.send(stop);
}

//! Lua hot-reload: tell the running player to recompile-and-swap
void reloadRemoteScripts(PlaySession& session, EditorConsole& console)
{
	if (!session.client.isConnected())
	{
		return;
	}
	// reload-ALL (v1): no FIELD_ID means every ScriptComponent on the player
	session.client.send(Orkige::DebugMessage(Protocol::MSG_RELOAD_SCRIPT));
	SDL_Log("orkige_editor: script change detected - reload sent to the player");
	console.addLine(ConsoleLevel::Info,
		"[reload] scripts changed - hot-reloading the running player");
	// optimistic: assume the reload heals whatever was broken. The player
	// re-pushes script_error (which re-populates scriptErrorIds) ONLY if a
	// reload actually failed - so a healed edit clears the RED marker at once.
	session.scriptErrorIds.clear();
}

//! .oui hot-reload: tell the running player to reload one declarative screen
void reloadRemoteUi(PlaySession& session, EditorConsole& console,
	std::string const& ouiName)
{
	if (!session.client.isConnected())
	{
		return;
	}
	Orkige::DebugMessage reload(Protocol::MSG_RELOAD_UI);
	reload.set(Protocol::FIELD_PATH, ouiName);
	session.client.send(reload);
	SDL_Log("orkige_editor: .oui change detected (%s) - reload sent to the "
		"player", ouiName.c_str());
	console.addLine(ConsoleLevel::Info,
		"[reload] ui '" + ouiName + "' changed - hot-reloading the running "
		"player");
}

namespace
{
//! @brief scan the project tree for `.oui` layouts, folding to the newest write
//! time per BASENAME (the name a game passes to GuiFactory::loadLayout / the
//! player resolves). Build trees are skipped so a native module's build output
//! never trips the watcher. @return basename -> newest file-time count.
std::map<std::string, long long> scanProjectOuiFiles(std::string const& root)
{
	std::map<std::string, long long> result;
	std::error_code ec;
	if (!std::filesystem::is_directory(root, ec))
	{
		return result;
	}
	for (std::filesystem::recursive_directory_iterator
		it(root, ec), end; it != end; it.increment(ec))
	{
		if (ec)
		{
			break;
		}
		// never descend into build outputs / VCS metadata
		if (it->is_directory(ec))
		{
			const std::string name = it->path().filename().string();
			if (name == "builds" || name == "build" || name == ".git")
			{
				it.disable_recursion_pending();
			}
			continue;
		}
		if (!it->is_regular_file(ec) || it->path().extension() != ".oui")
		{
			continue;
		}
		const std::filesystem::file_time_type mtime =
			std::filesystem::last_write_time(it->path(), ec);
		if (ec)
		{
			continue;
		}
		const long long stamp =
			static_cast<long long>(mtime.time_since_epoch().count());
		const std::string base = it->path().filename().string();
		auto found = result.find(base);
		if (found == result.end() || stamp > found->second)
		{
			result[base] = stamp;
		}
	}
	return result;
}

//! @brief poll the project tree for `.lua` (scripts/) and `.oui` edits (~4 Hz)
//! and hot-reload the running player on any change (MSG_RELOAD_SCRIPT reloads
//! ALL scripts; MSG_RELOAD_UI reloads just the changed layout). Desktop play
//! only (a device player has no host-filesystem trigger); the first poll of a
//! session only records the baseline (scriptsWatchArmed arms BOTH watchers) so
//! opening Play never fires a spurious reload.
void watchProjectScripts(PlaySession& session, EditorConsole& console,
	std::chrono::steady_clock::time_point now)
{
	if (session.projectRoot.empty() || session.onAndroid ||
		session.onSimulator || !session.client.isConnected())
	{
		return;
	}
	if (session.scriptsWatchArmed &&
		now - session.lastScriptCheck < std::chrono::milliseconds(250))
	{
		return;
	}
	session.lastScriptCheck = now;
	// file_clock's epoch is implementation-defined - on libstdc++ it lies in
	// the FUTURE, making every real mtime count negative. A zero-initialized
	// fold would therefore never observe any file; start from the minimum.
	long long newest = std::numeric_limits<long long>::min();
	const std::filesystem::path scriptsDir =
		std::filesystem::path(session.projectRoot) / "scripts";
	std::error_code ec;
	if (std::filesystem::is_directory(scriptsDir, ec))
	{
		for (std::filesystem::recursive_directory_iterator
			it(scriptsDir, ec), end; it != end; it.increment(ec))
		{
			if (ec)
			{
				break;
			}
			if (!it->is_regular_file(ec) ||
				it->path().extension() != ".lua")
			{
				continue;
			}
			const std::filesystem::file_time_type mtime =
				std::filesystem::last_write_time(it->path(), ec);
			if (!ec)
			{
				newest = std::max(newest,
					static_cast<long long>(mtime.time_since_epoch().count()));
			}
		}
	}
	// the .oui layouts, per basename (see scanProjectOuiFiles)
	std::map<std::string, long long> ouiNow =
		scanProjectOuiFiles(session.projectRoot);
	if (!session.scriptsWatchArmed)
	{
		// first poll of the session: record the baseline, never reload
		session.scriptsNewestMtime = newest;
		session.uiFileMtimes = std::move(ouiNow);
		session.scriptsWatchArmed = true;
		return;
	}
	if (newest > session.scriptsNewestMtime)
	{
		session.scriptsNewestMtime = newest;
		reloadRemoteScripts(session, console);
	}
	// per-layout diff: a new or freshly-written .oui hot-reloads just that screen
	for (auto const& entry : ouiNow)
	{
		auto known = session.uiFileMtimes.find(entry.first);
		if (known == session.uiFileMtimes.end() || entry.second > known->second)
		{
			reloadRemoteUi(session, console, entry.first);
		}
	}
	session.uiFileMtimes = std::move(ouiNow);
}
} // namespace

//! per-frame play session pump: complete the connect, drain streamed
//! messages (remote log lines land in the Console tagged "[remote]"), watch
//! the process and the link, enforce the stop grace timeout
void updatePlaySession(PlaySession& session, EditorConsole& console)
{
	if (!session.isActive())
	{
		return;
	}
	session.client.update();
	Orkige::DebugMessage message;
	while (session.client.receive(message))
	{
		if (message.type == Protocol::MSG_HELLO)
		{
			session.helloReceived = true;
			session.remoteScenePath = message.get(Protocol::FIELD_SCENE);
			if (message.version != Protocol::VERSION)
			{
				SDL_Log("orkige_editor: play - protocol version mismatch "
					"(player %d, editor %d)", message.version,
					Protocol::VERSION);
			}
		}
		else if (message.type == Protocol::MSG_HIERARCHY)
		{
			session.remoteHierarchy = message.getList(Protocol::LIST_IDS);
			// additive tree extension: old players send neither list - the
			// panel then falls back to the historical flat rendering
			session.remoteParents = message.getList(Protocol::LIST_PARENTS);
			session.remoteActive = message.getList(Protocol::LIST_ACTIVE);
			if (session.remoteParents.size() != session.remoteHierarchy.size())
			{
				session.remoteParents.clear();
			}
			if (session.remoteActive.size() != session.remoteHierarchy.size())
			{
				session.remoteActive.clear();
			}
			session.hierarchyReceived = true;
		}
		else if (message.type == Protocol::MSG_OBJECT_STATE)
		{
			session.stateObjectId = message.get(Protocol::FIELD_ID);
			session.stateComponents = message.getList(Protocol::LIST_COMPONENTS);
			session.stateProperties.clear();
			for (auto const& [key, value] : message.fields)
			{
				if (key != Protocol::FIELD_ID)
				{
					session.stateProperties[key] = value;
				}
			}
			// reflection metadata: four parallel lists describe
			// each streamed property so the Inspector picks a typed widget. A
			// player predating them leaves these empty (read-only-dump fallback).
			session.statePropKeys = message.getList(Protocol::LIST_PROP_KEYS);
			const Orkige::StringVector& kinds =
				message.getList(Protocol::LIST_PROP_KINDS);
			const Orkige::StringVector& hints =
				message.getList(Protocol::LIST_PROP_HINTS);
			const Orkige::StringVector& flags =
				message.getList(Protocol::LIST_PROP_FLAGS);
			session.statePropKind.clear();
			session.statePropHint.clear();
			session.statePropReadonly.clear();
			for (std::size_t i = 0; i < session.statePropKeys.size(); ++i)
			{
				const std::string& key = session.statePropKeys[i];
				if (i < kinds.size())
				{
					session.statePropKind[key] = std::atoi(kinds[i].c_str());
				}
				if (i < hints.size())
				{
					session.statePropHint[key] = hints[i];
				}
				if (i < flags.size() && flags[i] == "1")
				{
					session.statePropReadonly.insert(key);
				}
			}
		}
		else if (message.type == Protocol::MSG_LOG ||
			message.type == Protocol::MSG_ERROR)
		{
			// player log lines go into the Console tagged [remote]; severity
			// travels in the "level" field (errors always colour as errors)
			const std::string levelText = message.get(Protocol::FIELD_LEVEL);
			ConsoleLevel level = ConsoleLevel::Info;
			if (message.type == Protocol::MSG_ERROR || levelText == "error")
			{
				level = ConsoleLevel::Error;
			}
			else if (levelText == "warning")
			{
				level = ConsoleLevel::Warning;
			}
			console.addLine(level,
				"[remote] " + message.get(Protocol::FIELD_MESSAGE));
			session.remoteLogSeen = true;
		}
		else if (message.type == Protocol::MSG_SCRIPT_ERROR)
		{
			// a ScriptComponent failed in the player - make it LOUD: one RED
			// Console line per object per session (the player already dedupes
			// per connection; the set guards against reconnect repeats), plus
			// the toolbar marker and the hierarchy tint fed by scriptErrorIds
			const std::string id = message.get(Protocol::FIELD_ID);
			if (session.scriptErrorIds.insert(id).second)
			{
				console.addLine(ConsoleLevel::Error,
					"[remote] SCRIPT ERROR on '" + id + "': " +
					message.get(Protocol::FIELD_MESSAGE));
			}
		}
		else if (message.type == Protocol::MSG_SCREENSHOT_SAVED)
		{
			// the running game confirmed (or failed) a requested capture; record
			// it so the toolbar and the MCP screenshot_game poller see the fresh
			// result
			session.lastScreenshotPath = message.get(Protocol::FIELD_PATH);
			session.lastScreenshotOk =
				message.get(Protocol::FIELD_VALUE) == "1";
			session.lastScreenshotError = message.get(Protocol::FIELD_MESSAGE);
			++session.screenshotSeq;
			if (session.lastScreenshotOk)
			{
				console.addLine(ConsoleLevel::Info,
					"[remote] screenshot saved: " + session.lastScreenshotPath);
			}
			else
			{
				console.addLine(ConsoleLevel::Error,
					"[remote] screenshot FAILED: " + session.lastScreenshotError);
			}
		}
		else if (message.type == Protocol::MSG_RECORD_SAVED)
		{
			// the running game confirmed (or failed) a trace; record it so the
			// MCP record_trace poller sees the fresh artifact
			session.lastRecordPath = message.get(Protocol::FIELD_PATH);
			session.lastRecordOk = message.get(Protocol::FIELD_VALUE) == "1";
			session.lastRecordError = message.get(Protocol::FIELD_MESSAGE);
			session.recordingActive = false;
			++session.recordSeq;
			if (session.lastRecordOk)
			{
				console.addLine(ConsoleLevel::Info,
					"[remote] trace saved: " + session.lastRecordPath);
			}
			else
			{
				console.addLine(ConsoleLevel::Error,
					"[remote] trace FAILED: " + session.lastRecordError);
			}
		}
		else if (message.type == Protocol::MSG_STATS)
		{
			// periodic runtime metrics: the process memory footprint the Stats
			// panel and get_state surface. Absent fields (a player predating the
			// metric or a platform without a memory query) leave the values at
			// -1 (n/a); an empty string parses to 0, so guard on presence.
			if (message.has(Protocol::FIELD_MEM_RSS))
			{
				session.remoteMemRss = std::strtoll(
					message.get(Protocol::FIELD_MEM_RSS).c_str(), nullptr, 10);
			}
			if (message.has(Protocol::FIELD_MEM_RSS_PEAK))
			{
				session.remoteMemRssPeak = std::strtoll(
					message.get(Protocol::FIELD_MEM_RSS_PEAK).c_str(),
					nullptr, 10);
			}
			// window size + safe-area insets (pixels): the notch-safe box an
			// agent asserts the HUD against via get_safe_area
			auto readInt = [&](Orkige::String const& field, long long& out)
			{
				if (message.has(field))
				{
					out = std::strtoll(message.get(field).c_str(), nullptr, 10);
				}
			};
			readInt(Protocol::FIELD_WINDOW_W, session.remoteWindowW);
			readInt(Protocol::FIELD_WINDOW_H, session.remoteWindowH);
			readInt(Protocol::FIELD_SAFE_LEFT, session.remoteSafeLeft);
			readInt(Protocol::FIELD_SAFE_TOP, session.remoteSafeTop);
			readInt(Protocol::FIELD_SAFE_RIGHT, session.remoteSafeRight);
			readInt(Protocol::FIELD_SAFE_BOTTOM, session.remoteSafeBottom);
			// engine-level allocation counters + frame time (additive fields;
			// an older player leaves the -1 "not reported" defaults)
			readInt(Protocol::FIELD_ALLOC_PER_FRAME, session.remoteAllocPerFrame);
			readInt(Protocol::FIELD_ALLOC_PEAK, session.remoteAllocPeak);
			if (message.has(Protocol::FIELD_FRAME_MS))
			{
				session.remoteFrameMs = std::strtod(
					message.get(Protocol::FIELD_FRAME_MS).c_str(), nullptr);
			}
			// the running game's current named state (Lua game.setState); the
			// field is omitted while unset, so only overwrite when present
			if (message.has(Protocol::FIELD_GAME_STATE))
			{
				session.remoteGameState =
					message.get(Protocol::FIELD_GAME_STATE);
			}
			const Orkige::StringVector& allocTags =
				message.getList(Protocol::LIST_ALLOC_TAGS);
			const Orkige::StringVector& allocCounts =
				message.getList(Protocol::LIST_ALLOC_COUNTS);
			if (!allocTags.empty())
			{
				session.remoteAllocTags.clear();
				session.remoteAllocCounts.clear();
				for (std::size_t i = 0;
					i < allocTags.size() && i < allocCounts.size(); ++i)
				{
					session.remoteAllocTags.push_back(allocTags[i]);
					session.remoteAllocCounts.push_back(
						std::strtoll(allocCounts[i].c_str(), nullptr, 10));
				}
			}
			// streamed-music snapshot (parallel ids/files/info lists; each info a
			// flat "playing pos dur base group eff loop" string): the get_state
			// music surface. Rebuilt each MSG_STATS; empty when nothing streams.
			const Orkige::StringVector& musicIds =
				message.getList(Protocol::LIST_MUSIC_IDS);
			const Orkige::StringVector& musicFiles =
				message.getList(Protocol::LIST_MUSIC_FILES);
			const Orkige::StringVector& musicInfo =
				message.getList(Protocol::LIST_MUSIC_INFO);
			session.remoteMusic.clear();
			for (std::size_t i = 0; i < musicIds.size(); ++i)
			{
				PlaySession::RemoteMusicTrack track;
				track.id = musicIds[i];
				track.file = (i < musicFiles.size()) ? musicFiles[i] : "";
				if (i < musicInfo.size())
				{
					std::istringstream infoStream(musicInfo[i]);
					int playing = 0;
					int loop = 1;
					infoStream >> playing >> track.positionSec
						>> track.durationSec >> track.baseGain
						>> track.groupVolume >> track.effectiveGain >> loop;
					track.playing = playing != 0;
					track.loop = loop != 0;
				}
				session.remoteMusic.push_back(track);
			}
		}
		else if (message.type == Protocol::MSG_UI_LAYOUT)
		{
			// gui widget rects (parallel ids/rects, each rect a flat
			// "left top width height visible" string): the get_ui_layout source
			const Orkige::StringVector& ids =
				message.getList(Protocol::LIST_UI_IDS);
			const Orkige::StringVector& rects =
				message.getList(Protocol::LIST_UI_RECTS);
			session.remoteUiLayout.clear();
			for (std::size_t i = 0; i < ids.size() && i < rects.size(); ++i)
			{
				PlaySession::RemoteWidgetRect widget;
				widget.id = ids[i];
				std::istringstream rectStream(rects[i]);
				int visible = 1;
				int enabled = 1;
				int modal = 0;
				rectStream >> widget.left >> widget.top >> widget.width
					>> widget.height >> visible;
				// enabled/modal are additive trailing fields: an older player
				// that streams only five keeps the defaults (interactive,
				// non-modal)
				if (rectStream >> enabled)
				{
					widget.enabled = enabled != 0;
				}
				if (rectStream >> modal)
				{
					widget.modal = modal != 0;
				}
				widget.visible = visible != 0;
				session.remoteUiLayout.push_back(widget);
			}
			// the screen router's state (additive fields; an older player that
			// omits them leaves these empty)
			session.remoteScreenCurrent =
				message.get(Protocol::FIELD_UI_SCREEN);
			session.remoteScreenStack =
				message.get(Protocol::FIELD_UI_SCREEN_STACK);
		}
		else if (message.type == Protocol::MSG_PROFILE_DATA)
		{
			// the CPU frame profile (parallel names/info lists, each info a
			// flat "depth calls ms maxMs" string): the get_profile source
			const Orkige::StringVector& names =
				message.getList(Protocol::LIST_PROFILE_NAMES);
			const Orkige::StringVector& infos =
				message.getList(Protocol::LIST_PROFILE_INFO);
			session.remoteProfile.clear();
			for (std::size_t i = 0; i < names.size() && i < infos.size(); ++i)
			{
				PlaySession::RemoteProfileNode node;
				node.name = names[i];
				std::istringstream infoStream(infos[i]);
				infoStream >> node.depth >> node.calls >> node.milliseconds
					>> node.maxMilliseconds;
				session.remoteProfile.push_back(node);
			}
			if (message.has(Protocol::FIELD_FRAME_MS))
			{
				session.remoteProfileFrameMs = std::strtod(
					message.get(Protocol::FIELD_FRAME_MS).c_str(), nullptr);
			}
			++session.profileSeq;
		}
		else if (message.type == Protocol::MSG_BYE)
		{
			SDL_Log("orkige_editor: play - player said bye");
		}
		// unknown message types fall through silently on purpose: newer
		// players may add message types (the protocol grows additively)
	}
	int exitCode = 0;
	const bool processExited = session.process &&
		SDL_WaitProcess(session.process, false, &exitCode);
	const std::chrono::steady_clock::time_point now =
		std::chrono::steady_clock::now();
	switch (session.mode)
	{
	case PlaySession::Mode::Building:
	{
		// native module compile-on-Play: stream the compiler output into
		// the Console, then either run the next step, launch the built
		// executable, or - on a nonzero exit - revert to edit mode with the
		// errors on record and NOTHING launched
		pumpBuildOutput(session, console, false);
		int buildExit = 0;
		if (!session.buildProcess ||
			!SDL_WaitProcess(session.buildProcess, false, &buildExit))
		{
			return; // still compiling; the editor stays responsive
		}
		pumpBuildOutput(session, console, true); // drain the tail
		SDL_DestroyProcess(session.buildProcess);
		session.buildProcess = nullptr;
		if (buildExit != 0)
		{
			console.addLine(ConsoleLevel::Error, "[build] native module '" +
				session.nativeTarget + "' failed (exit " +
				std::to_string(buildExit) + ") - staying in edit mode");
			// record the failure BEFORE the teardown reverts to edit mode; the
			// outcome survives endPlaySession for get_state to report
			session.buildOutcome = PlaySession::BuildOutcome::Failed;
			endPlaySession(session, "native build failed");
			return;
		}
		if (!session.buildSteps.empty())
		{
			startNextBuildStep(session);
			return;
		}
		session.buildOutcome = PlaySession::BuildOutcome::Ok;
		console.addLine(ConsoleLevel::Info, "[build] native module '" +
			session.nativeTarget + "' built - launching " +
			session.nativeExecutable);
		spawnDesktopPlayProcess(session, session.nativeExecutable);
		return;
	}
	case PlaySession::Mode::Launching:
#ifdef __APPLE__
		// simulator preparation (boot / install) runs before any launch or
		// connect attempt - the pump reports its own errors and timeout
		if (session.onSimulator &&
			session.simPrep != PlaySession::SimPrep::None)
		{
			advanceSimulatorPrep(session);
			return;
		}
#endif
		if (processExited)
		{
			endPlaySession(session, "player exited during launch (code " +
				std::to_string(exitCode) + ")");
			return;
		}
		// Android: the editor connects to adb's HOST-side forward listener,
		// which accepts immediately even while the app is still booting -
		// adb then drops the bridge when the device-side connect fails. A
		// TCP connect is therefore no proof of a live player there; only the
		// protocol hello is. Pre-hello drops simply feed the retry loop.
		if (session.client.isConnected() &&
			(!session.onAndroid || session.helloReceived))
		{
			session.mode = PlaySession::Mode::Playing;
			SDL_Log("orkige_editor: play - connected to the player");
			return;
		}
		// the player needs a few seconds to boot before it listens: keep
		// re-connecting (a refused attempt ends in Failed, not Connecting).
		// NEVER reconnect while a socket is already live: on Android we sit
		// here CONNECTED-but-pre-hello (the gate above also needs helloReceived),
		// and DebugClient::connect() tears the live socket down first - churning
		// it every retry would drop each freshly-accepted connection before the
		// player can deliver its hello. A player whose frame loop is slow to
		// service the accept (e.g. the first FIFO-present frames on a software
		// GPU) then never completes the handshake. Hold the connection and wait
		// for the hello; if adb drops the bridge (device not up yet) the state
		// falls back to Disconnected and the retry naturally resumes.
		if (!session.client.isConnected() && !session.client.isConnecting() &&
			now - session.lastConnectAttempt >
				std::chrono::milliseconds(PLAY_CONNECT_RETRY_MS))
		{
			session.client.connect("127.0.0.1", session.port);
			session.lastConnectAttempt = now;
		}
		if (now - session.launchStart >
			std::chrono::seconds(PLAY_CONNECT_TIMEOUT_SECONDS))
		{
			endPlaySession(session, "could not connect to the player");
		}
		return;
	case PlaySession::Mode::Playing:
	case PlaySession::Mode::Paused:
		// Lua hot-reload: watch the project's scripts/ and tell the
		// running player to recompile-and-swap on any edit (desktop play only)
		watchProjectScripts(session, console, now);
		// crash resilience: a vanished process or a dropped link reverts
		// the editor to edit mode cleanly
		if (processExited)
		{
			endPlaySession(session, "player process exited unexpectedly "
				"(code " + std::to_string(exitCode) + ")");
			return;
		}
		if (!session.client.isConnected())
		{
			endPlaySession(session, "debug link dropped unexpectedly");
		}
		return;
	case PlaySession::Mode::Stopping:
		if (processExited)
		{
			endPlaySession(session, "stopped");
			return;
		}
		// simulator/Android sessions have no process handle - the link
		// closing is the quit confirmation (endPlaySession terminates the
		// remote app anyway, belt-and-braces)
		if ((session.onSimulator || session.onAndroid) &&
			!session.client.isConnected())
		{
			endPlaySession(session,
				session.onAndroid ? "stopped (android)" : "stopped (simulator)");
			return;
		}
		if (now - session.stopRequestTime >
			std::chrono::milliseconds(PLAY_STOP_GRACE_MS))
		{
			SDL_Log("orkige_editor: play - player ignored quit, killing it");
			endPlaySession(session, "stopped (killed after grace timeout)");
		}
		return;
	case PlaySession::Mode::Edit:
		return;
	}
}
