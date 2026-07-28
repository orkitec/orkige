/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalSession.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
// EditorTerminalSession.cpp - the pure Terminal-panel bookkeeping: title
// cleaning, agent classification, tab-label composition and post-close active
// index. UI-free and library-free so it links into orkige_editor_core and is
// unit-tested headlessly (EditorTerminalSessionTests).
#include "EditorTerminalSession.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

namespace OrkigeEditor
{
	namespace
	{
		//! the recognised terminal-agent CLI names (lower-case). A cleaned name
		//! whose leading run matches one of these by prefix draws the robot
		//! glyph. These name PROGRAMS the user runs - never a product referenced
		//! in UI text; the label the user sees is always runtime session data.
		const char* const kAgentNames[] = {
			"claude", "codex", "opencode", "aider", "gemini"
		};

		std::string toLower(std::string const& in)
		{
			std::string out;
			out.reserve(in.size());
			for (char c : in)
			{
				out.push_back(static_cast<char>(
					std::tolower(static_cast<unsigned char>(c))));
			}
			return out;
		}

		bool isSpace(char c)
		{
			return std::isspace(static_cast<unsigned char>(c)) != 0;
		}

		//! the basename of a (possibly trailing-slash) path
		std::string baseName(std::string const& path)
		{
			std::string p = path;
			while (!p.empty() && p.back() == '/')
			{
				p.pop_back();
			}
			const std::size_t slash = p.find_last_of('/');
			return slash == std::string::npos ? p : p.substr(slash + 1);
		}
	}

	std::string terminalCleanTitle(std::string const& raw)
	{
		std::size_t b = 0;
		std::size_t e = raw.size();
		while (b < e && isSpace(raw[b]))
		{
			++b;
		}
		while (e > b && isSpace(raw[e - 1]))
		{
			--e;
		}
		std::string s = raw.substr(b, e - b);
		if (s.empty())
		{
			return s;
		}
		// the leading whitespace-delimited token
		const std::size_t sp = s.find_first_of(" \t");
		const std::string first = (sp == std::string::npos) ? s : s.substr(0, sp);
		// a path or path-prefixed command line -> the leading app word only
		const bool looksPath =
			first.find('/') != std::string::npos ||
			(!first.empty() && first[0] == '~');
		if (looksPath)
		{
			const std::string base = baseName(first);
			return base.empty() ? first : base;
		}
		return s;
	}

	TerminalGlyphClass classifyTerminalApp(std::string const& name)
	{
		const std::string lower = toLower(name);
		for (const char* agent : kAgentNames)
		{
			const std::string a(agent);
			if (lower.size() >= a.size() && lower.compare(0, a.size(), a) == 0)
			{
				return TerminalGlyphClass::Agent;
			}
		}
		return TerminalGlyphClass::Terminal;
	}

	std::uint32_t terminalGlyphCodepoint(TerminalGlyphClass glyphClass)
	{
		// ICON_FA_ROBOT (U+f544) / ICON_FA_TERMINAL (U+f120) - both live in
		// EditorTheme.cpp's ICON_GLYPH_RANGES so the atlas bakes them.
		return glyphClass == TerminalGlyphClass::Agent ? 0xf544u : 0xf120u;
	}

	TerminalTabLabel terminalTabLabel(std::string const& vtTitle,
		std::string const& processName, int index1Based)
	{
		const std::string title = terminalCleanTitle(vtTitle);
		const std::string proc = terminalCleanTitle(processName);

		TerminalTabLabel out;
		if (classifyTerminalApp(proc) == TerminalGlyphClass::Agent ||
			classifyTerminalApp(title) == TerminalGlyphClass::Agent)
		{
			out.glyph = TerminalGlyphClass::Agent;
		}

		if (!title.empty())
		{
			out.text = title;
		}
		else if (!proc.empty())
		{
			out.text = proc;
		}
		else
		{
			out.text = "Terminal " + std::to_string(index1Based);
		}
		return out;
	}

	int terminalIndexAfterClose(int count, int closedIndex, int activeIndex)
	{
		const int newCount = count - 1;
		if (newCount <= 0)
		{
			return -1;
		}
		int active = activeIndex;
		if (active > closedIndex)
		{
			--active;	// everything past the removed slot slides down one
		}
		else if (active == closedIndex)
		{
			// the active tab itself went: keep the same slot (the next tab
			// slid into it) or fall back to the new last tab
			active = std::min(closedIndex, newCount - 1);
		}
		// active < closedIndex is unchanged
		return std::max(0, std::min(active, newCount - 1));
	}
}
