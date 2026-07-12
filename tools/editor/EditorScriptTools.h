// EditorScriptTools - discovery of EDITOR SCRIPT TOOLS (*.editor.lua).
//
// An editor tool is a project script whose file name ends in ".editor.lua": a
// one-shot command the human runs from the editor's Tools menu (and an agent
// runs over MCP with run_editor_script). This TU is the pure DISCOVERY half -
// a directory walk + filename/label parsing with no scripting or UI dependency,
// so it lives in the UI-independent orkige_editor_core library and is exercised
// headlessly by the editor-core unit tests. Execution (the sandbox + the
// editor.* verb table) lives in EditorScriptHost in the editor shell.
//
// It mirrors ScriptComponentRegistry's *.component.lua scan (same recursive
// walk, same "first of a duplicate name wins, both logged" rule) but registers
// NOTHING: an editor tool is not a component kind, just a listable command.
//
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#pragma once

#include <core_util/String.h>

#include <vector>

namespace Orkige
{
	//! @brief one discovered editor tool. `name` is the stable id (the file's
	//! base name with ".editor.lua" stripped: scripts/retag_tiles.editor.lua ->
	//! "retag_tiles"); `label` is the menu caption (the first-line
	//! "-- tool: <label>" comment when present, else a title-cased `name`);
	//! `path` is the ABSOLUTE file path the host loads.
	struct EditorScriptTool
	{
		String name;	//!< stable id (base name minus the suffix)
		String label;	//!< menu caption
		String path;	//!< absolute .editor.lua path
	};

	//! the filename suffix that marks a project script as an editor tool
	char const* editorToolSuffix();

	//! @brief the stable tool name for a file name/path: its base name with the
	//! ".editor.lua" suffix stripped; "" when the name is not a "*.editor.lua"
	//! file (a plain library, a *.component.lua, or any other file)
	String editorToolNameForFile(String const& fileName);

	//! @brief the default menu caption derived from a tool name: underscores and
	//! hyphens become spaces and each word is capitalised ("retag_tiles" ->
	//! "Retag Tiles"). Used when the file carries no "-- tool: <label>" line.
	String editorToolDefaultLabel(String const& toolName);

	//! @brief read a tool file's first-line label override: a leading
	//! "-- tool: <label>" comment (whitespace-tolerant) yields <label>; anything
	//! else (or an unreadable file) yields "". Only the FIRST line is consulted.
	String editorToolLabelOverride(String const& absolutePath);

	//! @brief (re)scan `scriptsDirectory` (absolute) recursively for
	//! "*.editor.lua" files and return the discovered tools sorted by label
	//! (then name). Stored paths are absolute. A duplicate tool name keeps the
	//! first file found (both are reported to the log). An empty/missing
	//! directory yields an empty list.
	std::vector<EditorScriptTool> scanEditorTools(String const& scriptsDirectory);
}
