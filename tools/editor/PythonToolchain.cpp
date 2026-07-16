// PythonToolchain - python3 preflight for the editor's subprocess use
// (see the header for the design). The parse/compare halves are pure and
// unit-tested; the probe spawns `<exe> --version` through SDL's process API
// (available here because orkige_editor_core links Orkige::Engine, which links
// SDL3 publicly).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "PythonToolchain.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdlib>

namespace Orkige
{
	//---------------------------------------------------------
	PythonVersion parsePythonVersion(String const& versionText)
	{
		PythonVersion result;
		// scan for the first run of "<digits>.<digits>" - tolerant of the
		// "Python " prefix, surrounding whitespace and a trailing patch/newline
		std::size_t i = 0;
		const std::size_t n = versionText.size();
		while (i < n)
		{
			if (!std::isdigit(static_cast<unsigned char>(versionText[i])))
			{
				++i;
				continue;
			}
			// parse major
			std::size_t j = i;
			int major = 0;
			while (j < n &&
				std::isdigit(static_cast<unsigned char>(versionText[j])))
			{
				major = major * 10 + (versionText[j] - '0');
				++j;
			}
			// a major.minor pair requires a '.' with a digit after it
			if (j >= n || versionText[j] != '.' || j + 1 >= n ||
				!std::isdigit(static_cast<unsigned char>(versionText[j + 1])))
			{
				i = j;	// not a version token - keep scanning
				continue;
			}
			++j; // consume '.'
			int minor = 0;
			while (j < n &&
				std::isdigit(static_cast<unsigned char>(versionText[j])))
			{
				minor = minor * 10 + (versionText[j] - '0');
				++j;
			}
			int patch = 0;
			// optional ".patch"
			if (j < n && versionText[j] == '.' && j + 1 < n &&
				std::isdigit(static_cast<unsigned char>(versionText[j + 1])))
			{
				++j;
				while (j < n &&
					std::isdigit(static_cast<unsigned char>(versionText[j])))
				{
					patch = patch * 10 + (versionText[j] - '0');
					++j;
				}
			}
			result.major = major;
			result.minor = minor;
			result.patch = patch;
			result.valid = true;
			return result;
		}
		return result; // invalid: no major.minor found
	}
	//---------------------------------------------------------
	bool pythonVersionAtLeast(PythonVersion const& v, int floorMajor,
		int floorMinor)
	{
		if (!v.valid)
		{
			return false;
		}
		if (v.major != floorMajor)
		{
			return v.major > floorMajor;
		}
		return v.minor >= floorMinor;
	}
	//---------------------------------------------------------
	String pythonExecutable()
	{
		const char* override = std::getenv("ORKIGE_PYTHON");
		if (override && override[0] != '\0')
		{
			return override;
		}
		return "python3";
	}
	//---------------------------------------------------------
	namespace
	{
		//! run `<executable> --version` capturing stdout+stderr; false when the
		//! process cannot be spawned. Mirrors the editor shell's
		//! runProcessCaptured, kept local so this TU stays in editor_core.
		bool runVersionCommand(String const& executable, String& output)
		{
			const char* argv[] = { executable.c_str(), "--version", nullptr };
			SDL_PropertiesID props = SDL_CreateProperties();
			SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER,
				const_cast<char**>(argv));
			SDL_SetNumberProperty(props,
				SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
			// older interpreters printed --version to stderr; fold it in
			SDL_SetBooleanProperty(props,
				SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
			SDL_Process* process = SDL_CreateProcessWithProperties(props);
			SDL_DestroyProperties(props);
			if (!process)
			{
				return false;
			}
			std::size_t outputSize = 0;
			int exitCode = 0;
			void* data = SDL_ReadProcess(process, &outputSize, &exitCode);
			output.assign(data ? static_cast<char*>(data) : "",
				data ? outputSize : 0);
			SDL_free(data);
			SDL_DestroyProcess(process);
			return true;
		}
	}
	//---------------------------------------------------------
	PythonProbeResult probePythonToolchainUncached(String const& executable,
		int floorMajor, int floorMinor)
	{
		PythonProbeResult result;
		result.executable = executable;

		const String floorText = std::to_string(floorMajor) + "." +
			std::to_string(floorMinor);
		const String needs = "the editor needs python3 >= " + floorText +
			" for asset import and project export (set ORKIGE_PYTHON to point at "
			"a suitable interpreter)";

		String output;
		if (!runVersionCommand(executable, output))
		{
			result.error = "python interpreter '" + executable +
				"' could not be launched - is python3 installed and on PATH? " +
				needs;
			return result;
		}
		result.version = parsePythonVersion(output);
		if (!result.version.valid)
		{
			String raw = output;
			// trim trailing whitespace/newlines for the message
			while (!raw.empty() &&
				std::isspace(static_cast<unsigned char>(raw.back())))
			{
				raw.pop_back();
			}
			result.error = "python interpreter '" + executable +
				"' reported no parseable version ('" + raw + "') - " + needs;
			return result;
		}
		if (!pythonVersionAtLeast(result.version, floorMajor, floorMinor))
		{
			const String found = std::to_string(result.version.major) + "." +
				std::to_string(result.version.minor) + "." +
				std::to_string(result.version.patch);
			result.error = "python interpreter '" + executable +
				"' is version " + found + ", too old - " + needs;
			return result;
		}
		result.ok = true;
		return result;
	}
	//---------------------------------------------------------
	std::vector<String> pythonFallbackCandidates()
	{
#if defined(__APPLE__)
		// an app launched from the Finder/Dock inherits the BARE system PATH
		// (/usr/bin:...), where python3 is the OS toolchain interpreter -
		// usually below the floor. The well-known package-manager locations
		// answer that launch honestly instead of demanding a terminal launch.
		return { "/opt/homebrew/bin/python3", "/usr/local/bin/python3" };
#else
		return {};
#endif
	}
	//---------------------------------------------------------
	PythonProbeResult const& probePythonToolchain()
	{
		// per-run cache: the interpreter does not change under a running editor
		static const PythonProbeResult cached = []()
		{
			// an explicit ORKIGE_PYTHON is an instruction, not a hint: probe
			// exactly it and report its failure without second-guessing
			const char* explicitPython = std::getenv("ORKIGE_PYTHON");
			if (explicitPython && explicitPython[0] != '\0')
			{
				return probePythonToolchainUncached(explicitPython);
			}
			PythonProbeResult primary = probePythonToolchainUncached("python3");
			if (primary.ok)
			{
				return primary;
			}
			for (String const& candidate : pythonFallbackCandidates())
			{
				PythonProbeResult fallback =
					probePythonToolchainUncached(candidate);
				if (fallback.ok)
				{
					return fallback;
				}
			}
			return primary;	// the PATH probe's message names the requirement
		}();
		return cached;
	}
}
