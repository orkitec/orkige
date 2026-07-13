// EditorDeviceTargets.cpp - play-target device enumeration: adb devices/
// emulators, iOS simulators (simctl) and gated iOS hardware (devicectl).
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

//! @brief run a command synchronously with captured stdout (used for the
//! short-lived simctl/adb/devicectl calls). False when the process cannot be
//! spawned; exitCode/output are only valid on true.
bool runProcessCaptured(const char* const* args, std::string& output,
	int& exitCode)
{
	SDL_Process* process = SDL_CreateProcess(args, true);
	if (!process)
	{
		return false;
	}
	size_t outputSize = 0;
	void* data = SDL_ReadProcess(process, &outputSize, &exitCode);
	output.assign(data ? static_cast<char*>(data) : "", data ? outputSize : 0);
	SDL_free(data);
	SDL_DestroyProcess(process);
	return true;
}

//! the BOUNDED sibling for the device probes: a first simctl/devicectl call
//! on a cold machine can take the better part of a minute (service spin-up),
//! and a probe that blocks past its caller's deadline reads as a failure. The
//! child is killed at the deadline and the probe reports false - an empty
//! device list, never a stall.
bool runProcessCapturedTimeout(std::vector<std::string> const& args,
	std::string& output, int& exitCode, unsigned int timeoutMs)
{
	std::vector<const char*> argv;
	argv.reserve(args.size() + 1);
	for (std::string const& arg : args)
	{
		argv.push_back(arg.c_str());
	}
	argv.push_back(nullptr);
	SDL_Process* process = SDL_CreateProcess(argv.data(), true);
	if (!process)
	{
		return false;
	}
	SDL_IOStream* stdoutStream = SDL_GetProcessOutput(process);
	output.clear();
	const Uint64 deadline = SDL_GetTicks() + timeoutMs;
	char buffer[4096];
	for (;;)
	{
		if (stdoutStream)
		{
			size_t got;
			while ((got = SDL_ReadIO(stdoutStream, buffer,
				sizeof(buffer))) > 0)
			{
				output.append(buffer, got);
			}
		}
		if (SDL_WaitProcess(process, false, &exitCode))
		{
			// drain whatever arrived between the last read and the exit
			if (stdoutStream)
			{
				size_t got;
				while ((got = SDL_ReadIO(stdoutStream, buffer,
					sizeof(buffer))) > 0)
				{
					output.append(buffer, got);
				}
			}
			SDL_DestroyProcess(process);
			return true;
		}
		if (SDL_GetTicks() >= deadline)
		{
			SDL_KillProcess(process, true);
			SDL_WaitProcess(process, true, &exitCode);
			SDL_DestroyProcess(process);
			SDL_Log("orkige_editor: device probe '%s' exceeded %ums - "
				"treated as no devices", args.empty() ? "" : args[0].c_str(),
				timeoutMs);
			return false;
		}
		SDL_Delay(25);
	}
}

//! vector-of-strings convenience wrapper around runProcessCaptured
bool runProcessCaptured(std::vector<std::string> const& args,
	std::string& output, int& exitCode)
{
	std::vector<const char*> argv;
	argv.reserve(args.size() + 1);
	for (std::string const& arg : args)
	{
		argv.push_back(arg.c_str());
	}
	argv.push_back(nullptr);
	return runProcessCaptured(argv.data(), output, exitCode);
}

//! adb from ANDROID_HOME (default: the per-user SDK), PATH as last resort
std::string adbPath()
{
	const char* androidHome = std::getenv("ANDROID_HOME");
	std::string sdk = androidHome ? androidHome : "";
	if (sdk.empty())
	{
		if (const char* home = std::getenv("HOME"))
		{
			sdk = std::string(home) + "/Library/Android/sdk";
		}
	}
	const std::string adb = sdk + "/platform-tools/adb";
	std::error_code ignored;
	if (!sdk.empty() && std::filesystem::exists(adb, ignored))
	{
		return adb;
	}
	return "adb";
}

//! @brief connected Android devices/emulators via 'adb devices -l'. Lines
//! look like "emulator-5554  device product:... model:sdk_gphone64_arm64 ..."
//! - only state "device" qualifies (offline/unauthorized are skipped). An
//! empty result also covers a missing adb - the picker then simply offers no
//! Android entries.
std::vector<AndroidDevice> listAdbDevices()
{
	std::vector<AndroidDevice> devices;
	std::string output;
	int exitCode = 0;
	if (!runProcessCapturedTimeout({ adbPath(), "devices", "-l" }, output,
		exitCode, 10000) ||
		exitCode != 0)
	{
		return devices;
	}
	std::istringstream stream(output);
	std::string line;
	while (std::getline(stream, line))
	{
		std::istringstream fields(line);
		AndroidDevice device;
		std::string state;
		if (!(fields >> device.serial >> state) || state != "device")
		{
			continue; // header, blank and non-ready lines
		}
		device.label = device.serial;
		std::string field;
		while (fields >> field)
		{
			if (field.rfind("model:", 0) == 0)
			{
				device.label = field.substr(6) + " (" + device.serial + ")";
			}
		}
		devices.push_back(std::move(device));
	}
	return devices;
}

//! true when the given adb device has the player APK installed
bool androidPlayerInstalled(std::string const& serial)
{
	std::string output;
	int exitCode = 0;
	return runProcessCaptured({ adbPath(), "-s", serial, "shell", "pm", "path",
		PLAY_ANDROID_PACKAGE }, output, exitCode) && exitCode == 0 &&
		output.find("package:") != std::string::npos;
}

#ifdef __APPLE__
//! @brief available iOS simulators (ANY state) via 'xcrun simctl list
//! devices available'. Only devices under the "-- iOS" runtime sections
//! qualify (tvOS/watchOS cannot run the player). Lines look like
//! "    iPhone 16 (137F509C-....) (Booted)"; transient states (Booting,
//! Shutting Down) are skipped. An empty result also covers simctl failures -
//! the picker then simply offers only "Desktop".
std::vector<SimulatorDevice> listSimulators()
{
	std::vector<SimulatorDevice> devices;
	std::string output;
	int exitCode = 0;
	if (!runProcessCapturedTimeout({ "/usr/bin/xcrun", "simctl", "list",
			"devices", "available" }, output, exitCode, 10000) ||
		exitCode != 0)
	{
		return devices;
	}
	std::istringstream stream(output);
	std::string line;
	bool inIosSection = false;
	while (std::getline(stream, line))
	{
		// strip trailing whitespace/CR (simctl pads the state suffix)
		const std::size_t lineEnd = line.find_last_not_of(" \t\r");
		if (lineEnd == std::string::npos)
		{
			continue;
		}
		line.resize(lineEnd + 1);
		if (line.rfind("-- ", 0) == 0)
		{
			inIosSection = (line.rfind("-- iOS", 0) == 0);
			continue;
		}
		if (!inIosSection)
		{
			continue;
		}
		// "<name> (<udid>) (Booted|Shutdown)"
		bool booted = false;
		std::size_t statePos = line.rfind(" (Booted)");
		if (statePos != std::string::npos)
		{
			booted = true;
		}
		else
		{
			statePos = line.rfind(" (Shutdown)");
		}
		if (statePos == std::string::npos || statePos < 3)
		{
			continue;
		}
		const std::size_t udidOpen = line.rfind(" (", statePos - 1);
		if (udidOpen == std::string::npos || statePos < udidOpen + 3)
		{
			continue;
		}
		SimulatorDevice device;
		device.booted = booted;
		device.udid = line.substr(udidOpen + 2, statePos - udidOpen - 3);
		device.name = line.substr(0, udidOpen);
		const std::size_t nameStart = device.name.find_first_not_of(" \t");
		device.name = (nameStart == std::string::npos)
			? "" : device.name.substr(nameStart);
		if (!device.name.empty() && !device.udid.empty())
		{
			devices.push_back(std::move(device));
		}
	}
	return devices;
}

//! is the simulator currently Booted? (false also when it is not in the
//! available-device list at all)
bool simulatorIsBooted(std::string const& udid)
{
	for (SimulatorDevice const& device : listSimulators())
	{
		if (device.udid == udid)
		{
			return device.booted;
		}
	}
	return false;
}

//! true when OrkigePlayer.app is installed on the (BOOTED) simulator -
//! 'simctl get_app_container' only answers for booted devices
bool simulatorPlayerInstalled(std::string const& udid)
{
	const char* args[] = { "/usr/bin/xcrun", "simctl", "get_app_container",
		udid.c_str(), PLAY_SIMULATOR_BUNDLE_ID, "app", nullptr };
	std::string output;
	int exitCode = 0;
	return runProcessCaptured(args, output, exitCode) && exitCode == 0;
}

//! @brief true when the player installed on the (BOOTED) simulator is at
//! least as new as the locally built one, so launching it is honest. A STALE
//! install must be replaced, never launched: it silently runs days-old
//! engine code (seen in the wild as a pre-project-system player rejecting
//! '--project' - black screen, instant exit). Compares the app binaries'
//! mtimes via the host filesystem (simulator containers live there); with
//! no local build to compare against the installed app counts as current.
bool simulatorPlayerUpToDate(std::string const& udid)
{
	const char* args[] = { "/usr/bin/xcrun", "simctl", "get_app_container",
		udid.c_str(), PLAY_SIMULATOR_BUNDLE_ID, "app", nullptr };
	std::string output;
	int exitCode = 0;
	if (!runProcessCaptured(args, output, exitCode) || exitCode != 0)
	{
		return false; // not installed at all
	}
	while (!output.empty() &&
		(output.back() == '\n' || output.back() == '\r' ||
			output.back() == ' '))
	{
		output.pop_back();
	}
	std::error_code error;
	const std::filesystem::file_time_type builtTime =
		std::filesystem::last_write_time(
			std::filesystem::path(ORKIGE_EDITOR_IOS_PLAYER_APP) /
				"OrkigePlayer", error);
	if (error)
	{
		return true; // nothing newer to offer - keep the install
	}
	const std::filesystem::file_time_type installedTime =
		std::filesystem::last_write_time(
			std::filesystem::path(output) / "OrkigePlayer", error);
	if (error)
	{
		return false; // container exists but the binary is gone - reinstall
	}
	return installedTime >= builtTime;
}

//--- iOS hardware (task: physical-device tooling, honestly gated) ----------

//! @brief is a codesigning identity available? Deploying to iOS HARDWARE
//! (unlike the simulator) requires the app to be signed with an Apple
//! Developer identity - without one the device entries stay disabled with an
//! explanatory tooltip.
bool hasCodesignIdentity()
{
	const char* args[] = { "/usr/bin/security", "find-identity", "-v", "-p",
		"codesigning", nullptr };
	std::string output;
	int exitCode = 0;
	if (!runProcessCaptured(args, output, exitCode) || exitCode != 0)
	{
		return false;
	}
	return output.find("0 valid identities found") == std::string::npos &&
		output.find("valid identities found") != std::string::npos;
}

//! @brief is a provisioning profile configured (path in
//! ORKIGE_IOS_PROVISIONING_PROFILE, and the file exists)? The identity/profile
//! are developer-machine specific and read from the environment, never the
//! committed project - see Docs/ios-signing.md.
bool hasProvisioningProfile()
{
	const char* profile = std::getenv("ORKIGE_IOS_PROVISIONING_PROFILE");
	if (!profile || !*profile)
	{
		return false;
	}
	std::error_code ignored;
	return std::filesystem::exists(profile, ignored);
}

//! iOS device signing is configured only when BOTH a codesigning identity and a
//! provisioning profile resolve
bool isIosSigningConfigured()
{
	return hasCodesignIdentity() && hasProvisioningProfile();
}

//! @brief connected iOS hardware via 'xcrun devicectl list devices'. The
//! human-readable table has fixed labels; rather than depending on column
//! widths, ask for the json dump and scan it crudely for the
//! identifier/name pairs (the editor has no JSON parser - the two keys are
//! unambiguous in devicectl's output). Empty on any failure.
std::vector<IosHardwareDevice> listIosHardwareDevices()
{
	std::vector<IosHardwareDevice> devices;
	const std::string jsonPath =
		(std::filesystem::temp_directory_path() / "orkige_devicectl.json")
			.string();
	std::string output;
	int exitCode = 0;
	if (!runProcessCapturedTimeout({ "/usr/bin/xcrun", "devicectl", "list",
			"devices", "--json-output", jsonPath }, output, exitCode,
			10000) || exitCode != 0)
	{
		return devices;
	}
	std::ifstream json(jsonPath);
	std::string text((std::istreambuf_iterator<char>(json)),
		std::istreambuf_iterator<char>());
	std::error_code ignored;
	std::filesystem::remove(jsonPath, ignored);
	// each device object carries "identifier" : "<udid>" and (deviceProperties)
	// "name" : "<user-visible name>"; scan pairwise
	std::size_t searchPos = 0;
	while (true)
	{
		const std::size_t idKey = text.find("\"identifier\"", searchPos);
		if (idKey == std::string::npos)
		{
			break;
		}
		IosHardwareDevice device;
		std::size_t valueStart = text.find('"', text.find(':', idKey) + 1);
		std::size_t valueEnd = (valueStart == std::string::npos)
			? std::string::npos : text.find('"', valueStart + 1);
		if (valueEnd != std::string::npos)
		{
			device.udid = text.substr(valueStart + 1, valueEnd - valueStart - 1);
		}
		const std::size_t nameKey = text.find("\"name\"", idKey);
		if (nameKey != std::string::npos)
		{
			valueStart = text.find('"', text.find(':', nameKey) + 1);
			valueEnd = (valueStart == std::string::npos)
				? std::string::npos : text.find('"', valueStart + 1);
			if (valueEnd != std::string::npos)
			{
				device.name =
					text.substr(valueStart + 1, valueEnd - valueStart - 1);
			}
		}
		if (!device.udid.empty() && !device.name.empty())
		{
			devices.push_back(std::move(device));
		}
		searchPos = idKey + 1;
	}
	return devices;
}

//! @brief install a (signed) .app on a USB-connected iOS device via 'xcrun
//! devicectl device install app'. On success the app's bundle identifier is
//! read back from the command's JSON dump (needed to launch it) - devicectl
//! reports the installed bundle under an "installedApplications" object, whose
//! bundleID is the only such key, so the crude scan is unambiguous (the editor
//! has no JSON parser, same approach as listIosHardwareDevices). Returns false
//! (with an operator-facing reason in 'error') when the install command fails.
bool iosHardwareInstallApp(std::string const& udid, std::string const& appPath,
	std::string& bundleId, std::string& error)
{
	const std::string jsonPath =
		(std::filesystem::temp_directory_path() / "orkige_devicectl_install.json")
			.string();
	std::string output;
	int exitCode = 0;
	const bool ran = runProcessCaptured({ "/usr/bin/xcrun", "devicectl",
		"device", "install", "app", "--device", udid, "--json-output", jsonPath,
		appPath }, output, exitCode);
	if (!ran)
	{
		error = "could not run 'xcrun devicectl device install app'";
		return false;
	}
	if (exitCode != 0)
	{
		error = "devicectl install failed (exit " + std::to_string(exitCode) +
			"): " + output;
		std::error_code ignored;
		std::filesystem::remove(jsonPath, ignored);
		return false;
	}
	std::ifstream json(jsonPath);
	const std::string text((std::istreambuf_iterator<char>(json)),
		std::istreambuf_iterator<char>());
	std::error_code ignored;
	std::filesystem::remove(jsonPath, ignored);
	const std::size_t key = text.find("\"bundleID\"");
	if (key != std::string::npos)
	{
		const std::size_t valueStart = text.find('"', text.find(':', key) + 1);
		const std::size_t valueEnd = (valueStart == std::string::npos)
			? std::string::npos : text.find('"', valueStart + 1);
		if (valueEnd != std::string::npos)
		{
			bundleId = text.substr(valueStart + 1, valueEnd - valueStart - 1);
		}
	}
	return true;
}

//! @brief launch an installed app on a USB-connected iOS device via 'xcrun
//! devicectl device process launch'. The game runs standalone from its bundled
//! scene - the editor cannot open a live debug link to it because a USB device
//! shares neither the host filesystem nor its loopback, and no dependency-free
//! CLI forwards a TCP port to it (see Docs/ios-signing.md). Returns false (with
//! a reason in 'error') on failure.
bool iosHardwareLaunchApp(std::string const& udid, std::string const& bundleId,
	std::string& error)
{
	std::string output;
	int exitCode = 0;
	const bool ran = runProcessCaptured({ "/usr/bin/xcrun", "devicectl",
		"device", "process", "launch", "--device", udid, bundleId },
		output, exitCode);
	if (!ran)
	{
		error = "could not run 'xcrun devicectl device process launch'";
		return false;
	}
	if (exitCode != 0)
	{
		error = "devicectl launch failed (exit " + std::to_string(exitCode) +
			"): " + output;
		return false;
	}
	return true;
}
#endif // __APPLE__
