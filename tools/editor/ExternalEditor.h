/********************************************************************
	created:	Monday 2026/07/13 at 12:00
	filename: 	ExternalEditor.h
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
*********************************************************************/
// ExternalEditor.h - the "open this file at line N in the user's code editor"
// service, pure and headless so the editor_core unit suite pins it down.
//
// Three concerns live here, all free of ImGui/SDL so they are testable:
//   * parseFileLineRefs   - find "path:line[:col]" references in a console line
//   * expandEditorCommand - substitute {file}/{line} into a command template
//   * resolveEditorCommand - pick HOW to open a file (configured template ->
//                            autodetected CLI -> platform file opener)
//   * readFileLinesAround - the source lines a quick-peek popup shows
// The actual detached process launch lives in the editor shell
// (ExternalEditorLaunch.cpp) since it needs SDL; this file never spawns.
#ifndef ORKIGE_EXTERNALEDITOR_H_12072026
#define ORKIGE_EXTERNALEDITOR_H_12072026

#include <functional>
#include <string>
#include <vector>

namespace Orkige
{
	//! @brief a parsed "path:line[:col]" reference located inside a line of
	//! console text. `path` is the text exactly as it appeared (absolute or
	//! project-relative); `begin`/`end` bound the whole reference in the source
	//! string. `line`/`column` are 1-based, 0 = none present.
	struct FileLineRef
	{
		std::string path;
		int line = 0;
		int column = 0;
		std::size_t begin = 0;		//!< [begin, end) span in the source text
		std::size_t end = 0;
	};

	//! @brief locate every "path:line[:col]" reference in one line of text. A
	//! path is a run of non-space, non-colon characters that carries a '.' or a
	//! '/' - so a bare "12:30" timestamp is NOT mistaken for one. Absolute and
	//! relative paths both match. Handles the Lua "scripts/x.lua:12:" and the
	//! compiler "/abs/x.cpp:45:9:" spellings.
	std::vector<FileLineRef> parseFileLineRefs(std::string const& text);

	//! @brief expand a command template into an argv list, substituting {file}
	//! and {line}. The template is split into tokens on ASCII whitespace FIRST
	//! (so a {file} standing in for a path with spaces stays ONE argv element),
	//! then the placeholders are filled per token. A line <= 0 strips a ":{line}"
	//! suffix (and any lone {line}) so "code -g {file}:{line}" opens the file
	//! without a bogus trailing colon. Empty tokens are dropped.
	std::vector<std::string> expandEditorCommand(std::string const& templ,
		std::string const& file, int line);

	//! probe seam: does an executable of this bare name resolve on PATH? Injected
	//! so the resolution tests never touch the real environment.
	using EditorPathProbe = std::function<bool(std::string const&)>;

	//! @brief the outcome of resolving how to open a file
	struct EditorCommandResolution
	{
		std::vector<std::string> argv;	//!< the process argv (never empty on success)
		bool opensAtLine = false;		//!< false = a plain opener, no line jump
		std::string source;				//!< "setting" / "detect:<exe>" / "opener"
	};

	//! @brief resolve the argv that opens `file` at `line`. Order:
	//!   1. the configured template, when non-empty (e.g. "code -g {file}:{line}")
	//!   2. the first autodetected CLI editor whose executable is on PATH
	//!   3. the platform file opener (macOS `open`, other desktops `xdg-open`),
	//!      which cannot jump to a line.
	//! The opener always yields an argv (it is assumed present); a spawn failure
	//! is reported by the caller.
	EditorCommandResolution resolveEditorCommand(
		std::string const& configuredTemplate, std::string const& file, int line,
		EditorPathProbe const& probe, bool macOS);

	//! @brief the autodetect candidate templates, in probe order (the leading
	//! token of each is the executable probed on PATH). Configuration data, not
	//! an endorsement of any tool. Exposed for the settings tooltip + tests.
	std::vector<std::string> const& externalEditorCandidates();

	//! @brief read up to `context` lines on each side of `targetLine` (1-based)
	//! from a text file. `outFirstLine` receives the 1-based number of the first
	//! returned line. Returns an empty vector when the file cannot be read.
	std::vector<std::string> readFileLinesAround(std::string const& path,
		int targetLine, int context, int& outFirstLine);

	//! @brief a path-looking token located under a hovered terminal cell.
	//! `path` is the trimmed path text (a leading '~' is NOT expanded here - the
	//! resolver does that); `line`/`column` are 1-based (0 = none). `begin`/`end`
	//! bound the token as a [begin, end) COLUMN span on the source line, for the
	//! hover underline.
	struct TerminalPathToken
	{
		std::string path;
		int line = 0;
		int column = 0;
		std::size_t begin = 0;
		std::size_t end = 0;
	};

	//! @brief extract the path-looking token under column @p col of one terminal
	//! screen line @p lineText (built so index == cell column; a wide/non-ASCII
	//! cell reads as a boundary). Expands from the hovered cell to whitespace,
	//! quote and bracket boundaries, trims trailing sentence punctuation, splits a
	//! trailing ":line[:col]" (reusing parseFileLineRefs), and REJECTS a token that
	//! does not look like a path (no '.', '/', '\\', or leading '~'). Returns false
	//! when the hovered cell is a boundary or the token is not path-like. Pure.
	bool terminalPathTokenAt(std::string const& lineText, std::size_t col,
		TerminalPathToken& out);

	//! @brief resolve a terminal path token to an existing file, in the order a
	//! terminal opens links: PROJECT-ROOT-relative first, then the SESSION CWD,
	//! then ABSOLUTE. A leading '~' expands against @p homeDir (when non-empty).
	//! @p exists is the injectable file-existence probe (so this stays pure and
	//! unit-testable). Returns the resolved path, or "" when nothing resolves.
	std::string resolveTerminalPath(std::string const& tokenPath,
		std::string const& projectRoot, std::string const& sessionCwd,
		std::string const& homeDir,
		std::function<bool(std::string const&)> const& exists);
}

#endif // ORKIGE_EXTERNALEDITOR_H_12072026
