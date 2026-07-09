// EditorPlaySession.cpp - the editor's play mode: temp-scene spawn of the
// player (desktop/other-flavor/simulator/adb), the native-module
// compile-on-Play build queue, the debug-link pump and crash detection.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <core_debugnet/DebugServer.h>
#include <core_game/SceneSerializer.h>
#include <core_project/NativeModule.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
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
	session.remoteSelectedId.clear();
	session.stateObjectId.clear();
	session.stateComponents.clear();
	session.stateProperties.clear();
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
	auto emitLine = [&console](std::string const& text)
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
			endPlaySession(session, "native build failed");
			return;
		}
		if (!session.buildSteps.empty())
		{
			startNextBuildStep(session);
			return;
		}
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
		// re-connecting (a refused attempt ends in Failed, not Connecting)
		if (!session.client.isConnecting() &&
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
