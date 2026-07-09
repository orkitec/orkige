// EditorExport.cpp - the async project export job (Build menu ->
// Util/orkige_export.py), output streamed into the Console.
// Split out of main.cpp (mechanical decomposition, see EditorApp.h).
#include "EditorApp.h"

#include <string>
#include <vector>

//! @brief launch the exporter for the open project (async; false when it
//! cannot start). The build tree the exporter packages from is per-platform:
//! THIS editor's build tree for macOS, the ios-simulator-debug /
//! android-debug preset trees for the mobile targets - the exporter reports
//! honestly when one of those was never built.
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
	const std::string exporter =
		std::string(ORKIGE_EDITOR_ENGINE_ROOT) + "/Util/orkige_export.py";
	std::string engineBuild = ORKIGE_EDITOR_ENGINE_BUILD_DIR;
	if (platform == "ios-simulator")
	{
		engineBuild = std::string(ORKIGE_EDITOR_ENGINE_ROOT) +
			"/build/ios-simulator-debug";
	}
	else if (platform == "android")
	{
		engineBuild = std::string(ORKIGE_EDITOR_ENGINE_ROOT) +
			"/build/android-debug";
	}
	const std::vector<std::string> command = { "python3", exporter,
		"--project", project.getRootDirectory(), "--platform", platform,
		"--engine-build", engineBuild };
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
	// the SDL log hook mirrors this into the Console as an "[export]" line
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
#ifdef __APPLE__
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
