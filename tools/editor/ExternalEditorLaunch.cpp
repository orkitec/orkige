// ExternalEditorLaunch.cpp - the editor-shell side of the open-at-line service:
// a PATH probe, the detached process launch (SDL_CreateProcess, never blocking
// the editor and never piping its stdio into the child) and the project-ref ->
// absolute-path resolution the console/inspector callers share. The pure
// command resolution + file:line parsing live in ExternalEditor.{h,cpp}.
#include "EditorApp.h"
#include "ExternalEditor.h"

#include <SDL3/SDL.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace
{

//! is this filesystem entry a regular file that is executable? (on POSIX any of
//! the execute bits; on Windows regular-file existence stands in)
bool isExecutableFile(fs::path const& p)
{
	std::error_code ec;
	if (!fs::is_regular_file(p, ec))
	{
		return false;
	}
#ifndef _WIN32
	const fs::perms perms = fs::status(p, ec).permissions();
	return (perms & (fs::perms::owner_exec | fs::perms::group_exec |
		fs::perms::others_exec)) != fs::perms::none;
#else
	return true;
#endif
}

} // namespace

//! @brief does an executable of this bare name resolve on PATH? A name that
//! already carries a path separator is tested directly. The resolution seam
//! (resolveEditorCommand) calls this to autodetect an installed CLI editor.
bool isTextEditableAsset(std::string const& path)
{
	std::string ext = fs::path(path).extension().string();
	for (char& c : ext)
	{
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	// the engine's text-authored asset formats plus the common plain-text kinds
	static const char* const kinds[] = {
		".lua", ".oui", ".omat", ".oshape", ".oanim", ".oscene", ".oprefab",
		".ogui", ".olevels", ".oactions", ".olayers", ".orkproj", ".orkmeta",
		".xlf", ".txt", ".json", ".md", ".xml", ".ini", ".cfg", ".csv",
		".glsl", ".vert", ".frag", ".h", ".hpp", ".c", ".cpp", ".cc",
	};
	for (const char* kind : kinds)
	{
		if (ext == kind)
		{
			return true;
		}
	}
	return false;
}

bool editorExecutableOnPath(std::string const& name)
{
	if (name.empty())
	{
		return false;
	}
	if (name.find('/') != std::string::npos ||
		name.find('\\') != std::string::npos)
	{
		return isExecutableFile(fs::path(name));
	}
	const char* pathEnv = std::getenv("PATH");
	if (!pathEnv)
	{
		return false;
	}
#ifdef _WIN32
	const char sep = ';';
#else
	const char sep = ':';
#endif
	const std::string paths = pathEnv;
	std::string::size_type start = 0;
	while (start <= paths.size())
	{
		std::string::size_type end = paths.find(sep, start);
		const std::string dir = paths.substr(start,
			end == std::string::npos ? std::string::npos : end - start);
		if (!dir.empty())
		{
			const fs::path candidate = fs::path(dir) / name;
			if (isExecutableFile(candidate))
			{
				return true;
			}
#ifdef _WIN32
			for (const char* ext : { ".exe", ".cmd", ".bat" })
			{
				std::error_code ec;
				if (fs::is_regular_file(fs::path(dir) / (name + ext), ec))
				{
					return true;
				}
			}
#endif
		}
		if (end == std::string::npos)
		{
			break;
		}
		start = end + 1;
	}
	return false;
}

void openInExternalEditor(std::string const& absolutePath, int line,
	ViewSettings const& settings)
{
#ifdef __APPLE__
	const bool macOS = true;
#else
	const bool macOS = false;
#endif
	const Orkige::EditorCommandResolution resolution =
		Orkige::resolveEditorCommand(settings.externalEditor, absolutePath, line,
			[](std::string const& exe) { return editorExecutableOnPath(exe); },
			macOS);
	if (resolution.argv.empty())
	{
		oDebugWarn("editor.extern", 0, "no way to open '" << absolutePath <<
			"' in an external editor");
		return;
	}
	// build the null-terminated argv SDL_CreateProcess wants
	std::vector<const char*> argv;
	argv.reserve(resolution.argv.size() + 1);
	for (std::string const& arg : resolution.argv)
	{
		argv.push_back(arg.c_str());
	}
	argv.push_back(nullptr);
	// launch DETACHED: pipe_stdio=false leaves the child's stdio alone (the
	// editor never captures it) and destroying the handle right away does not
	// terminate the child - same fire-and-forget pattern as Reveal in Finder
	if (SDL_Process* process = SDL_CreateProcess(argv.data(), false))
	{
		SDL_DestroyProcess(process);
		if (line > 0 && !resolution.opensAtLine)
		{
			oDebugMsg("editor.extern", 0, "opened '" << absolutePath <<
				"' (this opener cannot jump to line " << line <<
				" - set an External Editor command to enable it)");
		}
	}
	else
	{
		oDebugError("editor.extern", 0, "could not launch external editor for '"
			<< absolutePath << "' - " << SDL_GetError());
	}
}

std::string resolveProjectFilePath(Orkige::Project const& project,
	std::string const& ref)
{
	if (ref.empty())
	{
		return ref;
	}
	// an absolute path (or a Windows drive path) passes straight through
	if (ref.front() == '/' || (ref.size() > 1 && ref[1] == ':'))
	{
		return ref;
	}
	if (!project.isLoaded())
	{
		return ref;
	}
	// a bare/relative ref (a ScriptComponent's script filename, a Lua-error
	// project-relative path): try it as-is against the root, then under the
	// conventional scripts/ and assets/ roots, and take the first that exists
	for (const char* prefix : { "", "scripts/", "assets/" })
	{
		const std::string absolute = project.resolvePath(prefix + ref);
		std::error_code ec;
		if (fs::exists(absolute, ec))
		{
			return absolute;
		}
	}
	return project.resolvePath(ref);
}
