// EditorScriptTools - editor-tool discovery (see the header).
// Part of orkige (orkitec Game Engine), (c) 2009-2026 orkitec
#include "EditorScriptTools.h"

#include <core_debug/DebugMacros.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>

namespace Orkige
{
	namespace
	{
		//! the marker suffix: a project script is an editor tool iff its file
		//! name ends in this
		const char* const kEditorToolSuffix = ".editor.lua";
	}
	//---------------------------------------------------------
	char const* editorToolSuffix()
	{
		return kEditorToolSuffix;
	}
	//---------------------------------------------------------
	String editorToolNameForFile(String const& fileName)
	{
		// reduce to the base file name so "scripts/a/retag.editor.lua" and
		// "retag.editor.lua" derive the same tool name
		const String base = std::filesystem::path(fileName).filename().string();
		const String suffix = kEditorToolSuffix;
		if (base.size() <= suffix.size())
		{
			return "";
		}
		if (base.compare(base.size() - suffix.size(), suffix.size(), suffix) != 0)
		{
			return "";	// a plain .lua library, a *.component.lua, or other file
		}
		return base.substr(0, base.size() - suffix.size());
	}
	//---------------------------------------------------------
	String editorToolDefaultLabel(String const& toolName)
	{
		String label;
		label.reserve(toolName.size());
		bool startWord = true;
		for (char c : toolName)
		{
			if (c == '_' || c == '-' || c == ' ')
			{
				if (!label.empty() && label.back() != ' ')
				{
					label.push_back(' ');
				}
				startWord = true;
				continue;
			}
			if (startWord)
			{
				label.push_back(static_cast<char>(std::toupper(
					static_cast<unsigned char>(c))));
				startWord = false;
			}
			else
			{
				label.push_back(c);
			}
		}
		return label.empty() ? toolName : label;
	}
	//---------------------------------------------------------
	String editorToolLabelOverride(String const& absolutePath)
	{
		std::ifstream file(absolutePath.c_str());
		if (!file)
		{
			return "";
		}
		std::string firstLine;
		std::getline(file, firstLine);
		// strip a trailing CR (a CRLF-authored file)
		if (!firstLine.empty() && firstLine.back() == '\r')
		{
			firstLine.pop_back();
		}
		// expect "-- tool: <label>" with tolerant leading/inner whitespace
		std::size_t pos = firstLine.find_first_not_of(" \t");
		if (pos == std::string::npos || firstLine.compare(pos, 2, "--") != 0)
		{
			return "";
		}
		pos = firstLine.find_first_not_of(" \t", pos + 2);
		const std::string tag = "tool:";
		if (pos == std::string::npos ||
			firstLine.compare(pos, tag.size(), tag) != 0)
		{
			return "";
		}
		pos = firstLine.find_first_not_of(" \t", pos + tag.size());
		if (pos == std::string::npos)
		{
			return "";
		}
		std::string label = firstLine.substr(pos);
		// trim a trailing run of whitespace
		const std::size_t end = label.find_last_not_of(" \t");
		if (end != std::string::npos)
		{
			label.erase(end + 1);
		}
		return label;
	}
	//---------------------------------------------------------
	std::vector<EditorScriptTool> scanEditorTools(String const& scriptsDirectory)
	{
		std::vector<EditorScriptTool> tools;
		if (scriptsDirectory.empty())
		{
			return tools;
		}
		std::error_code ec;
		if (!std::filesystem::is_directory(scriptsDirectory, ec))
		{
			return tools;	// no scripts/ folder yet - nothing to discover
		}
		std::map<String, String> seen;	//!< tool name -> first file (dedupe)
		for (std::filesystem::recursive_directory_iterator
			it(scriptsDirectory, ec), end; it != end && !ec; it.increment(ec))
		{
			if (!it->is_regular_file(ec))
			{
				continue;
			}
			const String fileName = it->path().filename().string();
			const String name = editorToolNameForFile(fileName);
			if (name.empty())
			{
				continue;	// not a *.editor.lua file
			}
			// keep the name space honest: the first of two files deriving one
			// tool name wins, both are logged, neither aborts the scan
			std::map<String, String>::const_iterator prior = seen.find(name);
			if (prior != seen.end())
			{
				oDebugMsg("editor", 0, "EditorScriptTools: two files derive the "
					"editor tool '" << name << "' - keeping '" << prior->second
					<< "', ignoring '" << it->path().string() << "'");
				continue;
			}
			const String path = it->path().string();
			seen[name] = path;
			EditorScriptTool tool;
			tool.name = name;
			tool.path = path;
			const String labelOverride = editorToolLabelOverride(path);
			tool.label = labelOverride.empty() ? editorToolDefaultLabel(name)
											   : labelOverride;
			tools.push_back(tool);
		}
		std::sort(tools.begin(), tools.end(),
			[](EditorScriptTool const& a, EditorScriptTool const& b)
			{
				if (a.label != b.label)
				{
					return a.label < b.label;
				}
				return a.name < b.name;
			});
		return tools;
	}
}
