/**************************************************************
	created:	2026/07/28 at 12:00
	filename: 	EditorTerminalSessionTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
//! The pure Terminal-panel bookkeeping driven headlessly: title cleaning down to
//! a tab label, the agent-CLI glyph classifier, tab-label composition (title vs
//! process-name signals, agent detection) and the post-close active index.
#include <catch2/catch_test_macros.hpp>

#include <EditorTerminalSession.h>

#include <cstddef>
#include <cstdint>
#include <string>

using namespace OrkigeEditor;

TEST_CASE("terminal title cleaning: paths reduce to the leading app word",
	"[unit][editor][terminal]")
{
	// a shell reporting its working directory -> the last path component
	CHECK(terminalCleanTitle("/Users/me/dev/orkige") == "orkige");
	CHECK(terminalCleanTitle("~/dev/orkige") == "orkige");
	// a trailing slash still resolves to the final component
	CHECK(terminalCleanTitle("/Users/me/dev/orkige/") == "orkige");
	// a full-path command line -> the program basename only (the app word)
	CHECK(terminalCleanTitle("/opt/homebrew/bin/fish -l") == "fish");
	CHECK(terminalCleanTitle("/usr/bin/vim README.md") == "vim");
}

TEST_CASE("terminal title cleaning: plain titles pass through trimmed",
	"[unit][editor][terminal]")
{
	CHECK(terminalCleanTitle("  Claude Code  ") == "Claude Code");
	CHECK(terminalCleanTitle("fish") == "fish");
	CHECK(terminalCleanTitle("") == "");
	CHECK(terminalCleanTitle("   ") == "");
	// no leading path -> the whole trimmed title survives (not just token 1)
	CHECK(terminalCleanTitle("npm run build") == "npm run build");
}

TEST_CASE("terminal app classifier: known agents get the agent class",
	"[unit][editor][terminal]")
{
	CHECK(classifyTerminalApp("claude") == TerminalGlyphClass::Agent);
	CHECK(classifyTerminalApp("codex") == TerminalGlyphClass::Agent);
	CHECK(classifyTerminalApp("opencode") == TerminalGlyphClass::Agent);
	CHECK(classifyTerminalApp("aider") == TerminalGlyphClass::Agent);
	CHECK(classifyTerminalApp("gemini") == TerminalGlyphClass::Agent);
	// case-insensitive prefix match: a versioned/decorated name still matches
	CHECK(classifyTerminalApp("Claude") == TerminalGlyphClass::Agent);
	CHECK(classifyTerminalApp("claude-1.2") == TerminalGlyphClass::Agent);
	CHECK(classifyTerminalApp("CLAUDE CODE") == TerminalGlyphClass::Agent);
}

TEST_CASE("terminal app classifier: shells and tools are plain terminals",
	"[unit][editor][terminal]")
{
	CHECK(classifyTerminalApp("fish") == TerminalGlyphClass::Terminal);
	CHECK(classifyTerminalApp("zsh") == TerminalGlyphClass::Terminal);
	CHECK(classifyTerminalApp("bash") == TerminalGlyphClass::Terminal);
	CHECK(classifyTerminalApp("vim") == TerminalGlyphClass::Terminal);
	CHECK(classifyTerminalApp("git") == TerminalGlyphClass::Terminal);
	CHECK(classifyTerminalApp("") == TerminalGlyphClass::Terminal);
	CHECK(classifyTerminalApp("orkige") == TerminalGlyphClass::Terminal);
}

TEST_CASE("terminal tab label: title wins, then process, then a fallback",
	"[unit][editor][terminal]")
{
	// a set title is the label (a path title still reduces to its app word)
	{
		const TerminalTabLabel l = terminalTabLabel("/Users/me/dev/orkige", "fish", 1);
		CHECK(l.text == "orkige");
		CHECK(l.glyph == TerminalGlyphClass::Terminal);
	}
	// no title -> the foreground process name
	{
		const TerminalTabLabel l = terminalTabLabel("", "fish", 3);
		CHECK(l.text == "fish");
	}
	// neither -> the numbered fallback (1-based index)
	{
		const TerminalTabLabel l = terminalTabLabel("", "", 2);
		CHECK(l.text == "Terminal 2");
		CHECK(l.glyph == TerminalGlyphClass::Terminal);
	}
}

TEST_CASE("terminal tab label: an agent is detected from either signal",
	"[unit][editor][terminal]")
{
	// agent named as the foreground process, shell set no title
	{
		const TerminalTabLabel l = terminalTabLabel("", "claude", 1);
		CHECK(l.text == "claude");
		CHECK(l.glyph == TerminalGlyphClass::Agent);
	}
	// agent that DID set a descriptive title -> title is the label, still agent
	{
		const TerminalTabLabel l = terminalTabLabel("Claude Code", "node", 1);
		CHECK(l.text == "Claude Code");
		CHECK(l.glyph == TerminalGlyphClass::Agent);
	}
	// a plain shell stays a terminal
	{
		const TerminalTabLabel l = terminalTabLabel("orkige", "fish", 1);
		CHECK(l.glyph == TerminalGlyphClass::Terminal);
	}
}

TEST_CASE("terminal post-close active index bookkeeping",
	"[unit][editor][terminal]")
{
	// closing a tab AFTER the active one leaves the active where it is
	CHECK(terminalIndexAfterClose(3, 2, 0) == 0);
	// closing a tab BEFORE the active one shifts it down one
	CHECK(terminalIndexAfterClose(3, 0, 2) == 1);
	// closing the ACTIVE tab selects the neighbour that slid into the slot
	CHECK(terminalIndexAfterClose(3, 1, 1) == 1);
	// closing the active LAST tab falls back to the new last
	CHECK(terminalIndexAfterClose(3, 2, 2) == 1);
	// closing the only tab empties the list
	CHECK(terminalIndexAfterClose(1, 0, 0) == -1);
}

namespace
{
	//! MIRRORS EditorTheme.cpp's ICON_GLYPH_RANGES for the two glyphs the
	//! terminal tab classifier draws - a codepoint outside the atlas ranges
	//! rasterises as a blank tofu box. Keep in step with EditorTheme.cpp
	//! whenever the classifier maps to a new glyph (the FileFormatIcon
	//! precedent).
	constexpr std::uint32_t kIconGlyphRanges[] = {
		0xf120, 0xf120,		// terminal (plain-shell glyph)
		0xf544, 0xf544,		// robot (agent-session glyph)
	};

	bool codepointInRanges(std::uint32_t cp)
	{
		for (std::size_t i = 0;
			i + 1 < sizeof(kIconGlyphRanges) / sizeof(kIconGlyphRanges[0]);
			i += 2)
		{
			if (cp >= kIconGlyphRanges[i] && cp <= kIconGlyphRanges[i + 1])
			{
				return true;
			}
		}
		return false;
	}
}

TEST_CASE("terminal glyph codepoints are baked in the icon atlas ranges",
	"[unit][editor][terminal]")
{
	// the classifier's two glyph classes map to the FA codepoints the panel
	// draws (ICON_FA_TERMINAL / ICON_FA_ROBOT); both must be in the atlas
	// glyph ranges or the tab shows a tofu box
	CHECK(terminalGlyphCodepoint(TerminalGlyphClass::Terminal) == 0xf120u);
	CHECK(terminalGlyphCodepoint(TerminalGlyphClass::Agent) == 0xf544u);
	CHECK(codepointInRanges(terminalGlyphCodepoint(TerminalGlyphClass::Terminal)));
	CHECK(codepointInRanges(terminalGlyphCodepoint(TerminalGlyphClass::Agent)));
}
