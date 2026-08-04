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
#include <vector>

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

TEST_CASE("terminal mouse->cell hit test survives absurd coordinates",
	"[unit][editor][terminal]")
{
	// a headless/unplaced ImGui rect can hand back +/-FLT_MAX-ish positions;
	// the division then overflows int and the cast would be undefined behavior
	// (the sanitizer's catch) - the float-space clamp keeps every input safe
	{
		const TerminalGridPoint h = terminalCellAtPoint(
			3.0e38f, -3.0e38f, 0.0f, 0.0f, 0.01f, 0.01f, 80, 30);
		CHECK(h.col == 80);
		CHECK(h.line == 0);
	}
	{
		const TerminalGridPoint h = terminalCellAtPoint(
			-3.0e38f, 3.0e38f, 0.0f, 0.0f, 0.01f, 0.01f, 80, 30);
		CHECK(h.col == 0);
		CHECK(h.line == 29);
	}
}

TEST_CASE("terminal mouse->cell hit test clamps to the grid",
	"[unit][editor][terminal]")
{
	// a 10px cell grid whose line-0 origin sits at (100,50); 80 cols, 30 lines
	// a point inside cell (col 3, line 2)
	{
		const TerminalGridPoint h = terminalCellAtPoint(
			135.0f, 75.0f, 100.0f, 50.0f, 10.0f, 10.0f, 80, 30);
		CHECK(h.col == 3);
		CHECK(h.line == 2);
	}
	// a point ABOVE and LEFT of the grid clamps to (0,0) - a drag past the top
	{
		const TerminalGridPoint h = terminalCellAtPoint(
			10.0f, 10.0f, 100.0f, 50.0f, 10.0f, 10.0f, 80, 30);
		CHECK(h.col == 0);
		CHECK(h.line == 0);
	}
	// a point far to the RIGHT clamps col to cols (an exclusive end stop) and
	// far BELOW clamps line to the last line
	{
		const TerminalGridPoint h = terminalCellAtPoint(
			9999.0f, 9999.0f, 100.0f, 50.0f, 10.0f, 10.0f, 80, 30);
		CHECK(h.col == 80);		// cols is a valid selection end
		CHECK(h.line == 29);	// totalLines - 1
	}
}

TEST_CASE("terminal drag holds its head when the pointer leaves the window",
	"[unit][editor][terminal]")
{
	TerminalGridPoint anchor;
	anchor.line = 0;
	anchor.col = 0;
	TerminalGridPoint head;
	head.line = 0;
	head.col = 11;
	// terminalCellAtPoint CLAMPS every input into the grid, so a pointer with
	// no position (-FLT_MAX, the leave convention) arrives here as cell (0,0).
	// Following it would collapse a live drag onto an anchor sitting there -
	// which reads as "still selecting" while the selected text is empty.
	TerminalGridPoint clampedAway;
	clampedAway.line = 0;
	clampedAway.col = 0;
	{
		const TerminalDragState d =
			terminalDragStep(anchor, head, clampedAway, false);
		CHECK(d.headLine == 0);
		CHECK(d.headCol == 11);		// held, not collapsed
		CHECK(d.hasSelection);
	}
	// and it resumes from the live pointer the moment it is back
	{
		TerminalGridPoint p;
		p.line = 2;
		p.col = 4;
		const TerminalDragState d = terminalDragStep(anchor, head, p, true);
		CHECK(d.headLine == 2);
		CHECK(d.headCol == 4);
		CHECK(d.hasSelection);
	}
}

TEST_CASE("terminal drag arms from anchor vs head, never a latch",
	"[unit][editor][terminal]")
{
	TerminalGridPoint anchor;
	anchor.line = 3;
	anchor.col = 5;
	// a head still on the anchor encloses nothing
	{
		const TerminalDragState d = terminalDragStep(anchor, anchor, anchor,
			true);
		CHECK_FALSE(d.hasSelection);
	}
	// dragging away arms it
	TerminalGridPoint away;
	away.line = 3;
	away.col = 16;
	const TerminalDragState armed = terminalDragStep(anchor, anchor, away,
		true);
	CHECK(armed.hasSelection);
	// ... and dragging BACK onto the anchor disarms it again: a latched flag
	// would leave the copy chord publishing an empty string to the pasteboard
	{
		TerminalGridPoint back;
		back.line = anchor.line;
		back.col = anchor.col;
		const TerminalDragState d = terminalDragStep(anchor, away, back, true);
		CHECK(d.headLine == anchor.line);
		CHECK(d.headCol == anchor.col);
		CHECK_FALSE(d.hasSelection);
	}
}

TEST_CASE("terminal agent classification maps names to specific agents",
	"[unit][editor][terminal]")
{
	CHECK(terminalAgentOf("claude") == TerminalAgent::Claude);
	CHECK(terminalAgentOf("codex") == TerminalAgent::Codex);
	CHECK(terminalAgentOf("opencode") == TerminalAgent::Opencode);
	CHECK(terminalAgentOf("aider") == TerminalAgent::Aider);
	CHECK(terminalAgentOf("gemini") == TerminalAgent::Gemini);
	CHECK(terminalAgentOf("Claude-1.2") == TerminalAgent::Claude);	// prefix
	CHECK(terminalAgentOf("fish") == TerminalAgent::None);
	CHECK(terminalAgentOf("") == TerminalAgent::None);
}

TEST_CASE("terminal agent badge codepoints are distinct private-use values",
	"[unit][editor][terminal]")
{
	// U+E000 + ordinal, one per recognised agent; None has no badge
	CHECK(terminalAgentBadgeCodepoint(TerminalAgent::Claude) == 0xE000u);
	CHECK(terminalAgentBadgeCodepoint(TerminalAgent::Codex) == 0xE001u);
	CHECK(terminalAgentBadgeCodepoint(TerminalAgent::Generic) == 0xE005u);
	CHECK(terminalAgentBadgeCodepoint(TerminalAgent::None) == 0u);
	// every recognised agent gets a UNIQUE codepoint (no tab collisions)
	const TerminalAgent all[] = { TerminalAgent::Claude, TerminalAgent::Codex,
		TerminalAgent::Opencode, TerminalAgent::Aider, TerminalAgent::Gemini,
		TerminalAgent::Generic };
	for (std::size_t i = 0; i < 6; ++i)
	{
		for (std::size_t j = i + 1; j < 6; ++j)
		{
			CHECK(terminalAgentBadgeCodepoint(all[i]) !=
				terminalAgentBadgeCodepoint(all[j]));
		}
	}
}

TEST_CASE("terminal agent badge mark construction parameters",
	"[unit][editor][terminal]")
{
	// the structural stroke counts the generators build the marks from
	CHECK(terminalAgentBadgeStrokeCount(TerminalAgent::Claude) == 8);	// spokes
	CHECK(terminalAgentBadgeStrokeCount(TerminalAgent::Codex) == 6);	// ring loops
	CHECK(terminalAgentBadgeStrokeCount(TerminalAgent::Opencode) == 0);	// monogram
	// each recognised agent carries a non-black signature tint
	const TerminalAgent all[] = { TerminalAgent::Claude, TerminalAgent::Codex,
		TerminalAgent::Opencode, TerminalAgent::Aider, TerminalAgent::Gemini,
		TerminalAgent::Generic };
	for (TerminalAgent a : all)
	{
		const TerminalBadgeTint t = terminalAgentTint(a);
		CHECK((t.r != 0 || t.g != 0 || t.b != 0));
	}
}

TEST_CASE("terminal agent badge pixels are non-empty, tinted and deterministic",
	"[unit][editor][terminal]")
{
	const int size = 24;
	const TerminalAgent all[] = { TerminalAgent::Claude, TerminalAgent::Codex,
		TerminalAgent::Opencode, TerminalAgent::Aider, TerminalAgent::Gemini,
		TerminalAgent::Generic };
	for (TerminalAgent a : all)
	{
		const std::vector<unsigned char> px = terminalAgentBadgePixels(a, size);
		REQUIRE(px.size() ==
			static_cast<std::size_t>(size) * size * 4);
		// SOME pixel is painted (alpha > 0) - the mark is not blank
		bool anyPainted = false;
		// the signature tint appears somewhere in the painted pixels
		const TerminalBadgeTint tint = terminalAgentTint(a);
		bool tintPresent = false;
		for (std::size_t i = 0; i < px.size(); i += 4)
		{
			if (px[i + 3] > 0)
			{
				anyPainted = true;
			}
			// a near-match to the tint (the mark's fill; monograms also carry a
			// near-white initial, so match with a tolerance on the field colour)
			const int dr = static_cast<int>(px[i + 0]) - tint.r;
			const int dg = static_cast<int>(px[i + 1]) - tint.g;
			const int db = static_cast<int>(px[i + 2]) - tint.b;
			if (px[i + 3] > 200 && dr * dr + dg * dg + db * db < 900)
			{
				tintPresent = true;
			}
		}
		CHECK(anyPainted);
		CHECK(tintPresent);
		// deterministic: the same request yields byte-identical pixels
		CHECK(terminalAgentBadgePixels(a, size) == px);
	}
	// a non-positive size yields no pixels
	CHECK(terminalAgentBadgePixels(TerminalAgent::Claude, 0).empty());
}

TEST_CASE("terminal title classifier matches agent names as whole words",
	"[unit][editor][terminal]")
{
	// a leading agent word (with or without trailing decoration) classifies
	CHECK(terminalAgentInTitle("claude") == TerminalAgent::Claude);
	CHECK(terminalAgentInTitle("Claude Code") == TerminalAgent::Claude);
	CHECK(terminalAgentInTitle("claude - /Users/me/dev") == TerminalAgent::Claude);
	// an agent word anywhere in the title (still a whole word)
	CHECK(terminalAgentInTitle("running codex now") == TerminalAgent::Codex);
	// a status ticker with no agent word does NOT classify
	CHECK(terminalAgentInTitle("Check open file") == TerminalAgent::None);
	CHECK(terminalAgentInTitle("") == TerminalAgent::None);
	// the boundary check: "raider" must not match "aider", "gemini" only whole
	CHECK(terminalAgentInTitle("raider quest") == TerminalAgent::None);
	CHECK(terminalAgentInTitle("codexample") == TerminalAgent::None);
}

TEST_CASE("terminal agent canonical display names",
	"[unit][editor][terminal]")
{
	CHECK(terminalAgentDisplayName(TerminalAgent::Claude) == "Claude");
	CHECK(terminalAgentDisplayName(TerminalAgent::Codex) == "Codex");
	CHECK(terminalAgentDisplayName(TerminalAgent::Opencode) == "OpenCode");
	CHECK(terminalAgentDisplayName(TerminalAgent::Aider) == "Aider");
	CHECK(terminalAgentDisplayName(TerminalAgent::Gemini) == "Gemini");
	CHECK(terminalAgentDisplayName(TerminalAgent::Generic) == "Agent");
	CHECK(terminalAgentDisplayName(TerminalAgent::None).empty());
}

TEST_CASE("terminal sticky classification: process is authoritative",
	"[unit][editor][terminal]")
{
	// a shell foreground never classifies
	CHECK(terminalUpdateStickyAgent(TerminalAgent::None, "fish", "") ==
		TerminalAgent::None);
	// launching claude (the foreground process) classifies the session
	CHECK(terminalUpdateStickyAgent(TerminalAgent::None, "claude", "") ==
		TerminalAgent::Claude);
	// a versioned foreground name still classifies (prefix)
	CHECK(terminalUpdateStickyAgent(TerminalAgent::None, "claude-1.2", "") ==
		TerminalAgent::Claude);
}

TEST_CASE("terminal sticky classification: status-ticker titles never declassify",
	"[unit][editor][terminal]")
{
	// classified as Claude, the foreground process is still claude, the title is
	// now a status ticker with no agent word - it STAYS Claude
	CHECK(terminalUpdateStickyAgent(TerminalAgent::Claude, "claude",
		"\xe2\x9c\xb3 Check open file") == TerminalAgent::Claude);
	// even if the process poll is momentarily empty (before the next poll), a
	// ticker title must not flip the classification off
	CHECK(terminalUpdateStickyAgent(TerminalAgent::Claude, "",
		"Compacting conversation") == TerminalAgent::Claude);
}

TEST_CASE("terminal sticky classification: title classifies without a process signal",
	"[unit][editor][terminal]")
{
	// no foreground-process signal (empty - a platform without one, or before the
	// first poll): an announce-in-title agent classifies from the title alone
	CHECK(terminalUpdateStickyAgent(TerminalAgent::None, "", "Claude Code") ==
		TerminalAgent::Claude);
	// but a plain shell title does not
	CHECK(terminalUpdateStickyAgent(TerminalAgent::None, "", "~/dev/orkige") ==
		TerminalAgent::None);
}

TEST_CASE("terminal sticky classification: an agent exit reverts to a shell",
	"[unit][editor][terminal]")
{
	// claude was classified; it exits and the foreground reverts to the login
	// shell -> the KNOWN SHELL name declassifies the session
	CHECK(terminalUpdateStickyAgent(TerminalAgent::Claude, "fish", "fish") ==
		TerminalAgent::None);
	// a full-path shell foreground also declassifies (cleaned to the app word)
	CHECK(terminalUpdateStickyAgent(TerminalAgent::Codex, "/bin/zsh", "") ==
		TerminalAgent::None);
}

TEST_CASE("terminal sticky classification: an interpreter foreground HOLDS - "
	"only a shell declassifies", "[unit][editor][terminal]")
{
	// the live claude shape: the launcher classifies as "claude", then execs to
	// its runtime - the foreground becomes "node". An unknown non-shell name is
	// the agent's own runtime, NOT the shell coming back: the classification
	// holds through the whole session (the owner's tab flipped to the ticker
	// exactly because "node" used to read as an exit)
	CHECK(terminalUpdateStickyAgent(TerminalAgent::Claude, "node",
		"\xe2\x9c\xb3 Check open file") == TerminalAgent::Claude);
	CHECK(terminalUpdateStickyAgent(TerminalAgent::Claude, "node", "") ==
		TerminalAgent::Claude);
	// other interpreters hold too
	CHECK(terminalUpdateStickyAgent(TerminalAgent::Aider, "python3.12", "") ==
		TerminalAgent::Aider);
	// an interpreter foreground on an UNCLASSIFIED session does not classify by
	// itself (only an agent process/title does)
	CHECK(terminalUpdateStickyAgent(TerminalAgent::None, "node", "") ==
		TerminalAgent::None);
	// the shell-name detector itself
	CHECK(terminalIsShellName("zsh"));
	CHECK(terminalIsShellName("Fish"));
	CHECK(terminalIsShellName("pwsh"));
	CHECK(!terminalIsShellName("node"));
	CHECK(!terminalIsShellName("python3.12"));
	CHECK(!terminalIsShellName(""));
}

TEST_CASE("terminal sticky tab label: classified shows badge + canonical name",
	"[unit][editor][terminal]")
{
	// a classified session's label is the STABLE canonical name + agent glyph,
	// regardless of the live status-ticker title the agent streams
	{
		const TerminalTabLabel l = terminalSessionTabLabel(TerminalAgent::Claude,
			"\xe2\x9c\xb3 Check open file", "claude", 1);
		CHECK(l.text == "Claude");
		CHECK(l.glyph == TerminalGlyphClass::Agent);
		CHECK(l.agent == TerminalAgent::Claude);
	}
	{
		const TerminalTabLabel l = terminalSessionTabLabel(TerminalAgent::Codex,
			"building...", "codex", 2);
		CHECK(l.text == "Codex");
		CHECK(l.agent == TerminalAgent::Codex);
	}
	// unclassified: today's title-then-process-then-numbered composition
	{
		const TerminalTabLabel l = terminalSessionTabLabel(TerminalAgent::None,
			"/Users/me/dev/orkige", "fish", 1);
		CHECK(l.text == "orkige");
		CHECK(l.glyph == TerminalGlyphClass::Terminal);
		CHECK(l.agent == TerminalAgent::None);
	}
	{
		const TerminalTabLabel l = terminalSessionTabLabel(TerminalAgent::None,
			"", "", 3);
		CHECK(l.text == "Terminal 3");
	}
}

TEST_CASE("terminal renderable-symbol filter drops what the UI font lacks",
	"[unit][editor][terminal]")
{
	// ordinary text is renderable
	CHECK(terminalIsRenderableSymbol('A'));
	CHECK(terminalIsRenderableSymbol(0x00e9));	// e-acute
	// control codes and the symbol/dingbat/emoji ranges are not
	CHECK_FALSE(terminalIsRenderableSymbol(0x1b));	// ESC
	CHECK_FALSE(terminalIsRenderableSymbol(0x2733));	// eight-spoked asterisk
	CHECK_FALSE(terminalIsRenderableSymbol(0x2728));	// sparkles
	CHECK_FALSE(terminalIsRenderableSymbol(0x1f680));	// rocket emoji

	// the sparkle status ticker filters down to plain text, leading symbol gone
	CHECK(terminalFilterRenderable("\xe2\x9c\xb3 Check open file") ==
		"Check open file");
	// an all-symbol title filters to empty (the caller falls back)
	CHECK(terminalFilterRenderable("\xe2\x9c\xb3\xe2\x9c\xa8").empty());
	// plain text passes through unchanged (and trimmed)
	CHECK(terminalFilterRenderable("  npm run build  ") == "npm run build");
	// an interior emoji is dropped, the surrounding text kept
	CHECK(terminalFilterRenderable("done \xf0\x9f\x9a\x80 shipping") ==
		"done  shipping");
}

TEST_CASE("terminal sticky tab label never leads with a tofu box",
	"[unit][editor][terminal]")
{
	// an unclassified session whose title leads with an un-renderable symbol has
	// it stripped, so the tab never leads with a '?'
	const TerminalTabLabel l = terminalSessionTabLabel(TerminalAgent::None,
		"\xe2\x9c\xb3 my task", "", 1);
	CHECK(l.text == "my task");
	CHECK(l.glyph == TerminalGlyphClass::Terminal);
}

// --- the terminal follow/pin contract ---------------------------------------
namespace
{
	//! spell one decision out compactly for the matrix
	TerminalFollowVerdict decide(bool wasFollowing, bool atBottom,
		bool contentGrew, bool userScrolledAway, bool isSelecting, bool sentInput)
	{
		TerminalFollowInputs in;
		in.wasFollowing = wasFollowing;
		in.atBottom = atBottom;
		in.contentGrew = contentGrew;
		in.userScrolledAway = userScrolledAway;
		in.isSelecting = isSelecting;
		in.sentInput = sentInput;
		return terminalFollowDecision(in);
	}
}

TEST_CASE("terminal follow: pinned view glues to a growing tail",
	"[unit][editor][terminal]")
{
	// WHILE PINNED and content grew (new output / resize / re-shown tab), the
	// view stays glued to the newest line and stays pinned.
	const TerminalFollowVerdict grew =
		decide(/*following*/true, /*atBottom*/true, /*grew*/true,
			/*scrolledAway*/false, /*selecting*/false, /*sentInput*/false);
	CHECK(grew.pinToBottom);
	CHECK(grew.followTail);

	// pinned but idle (no growth): stays pinned, issues no scroll
	const TerminalFollowVerdict idle =
		decide(true, true, false, false, false, false);
	CHECK_FALSE(idle.pinToBottom);
	CHECK(idle.followTail);

	// the pin survives an at-bottom READ going stale mid-growth: even reported
	// NOT-at-bottom (the one-frame ContentSize lag), a pinned view keeps
	// following as long as the user did not scroll away
	const TerminalFollowVerdict lag =
		decide(true, /*atBottom*/false, /*grew*/true, false, false, false);
	CHECK(lag.pinToBottom);
	CHECK(lag.followTail);
}

TEST_CASE("terminal follow: a user scroll-up unpins, returning re-pins",
	"[unit][editor][terminal]")
{
	// scrolling up UNPINS even while output keeps arriving
	const TerminalFollowVerdict away =
		decide(/*following*/true, /*atBottom*/false, /*grew*/true,
			/*scrolledAway*/true, false, false);
	CHECK_FALSE(away.pinToBottom);
	CHECK_FALSE(away.followTail);

	// unpinned + output growing while the user reads history: stays unpinned,
	// never yanks the view down
	const TerminalFollowVerdict reading =
		decide(/*following*/false, /*atBottom*/false, /*grew*/true, false,
			false, false);
	CHECK_FALSE(reading.pinToBottom);
	CHECK_FALSE(reading.followTail);

	// returning to within the epsilon of the bottom RE-PINS
	const TerminalFollowVerdict back =
		decide(/*following*/false, /*atBottom*/true, /*grew*/false, false,
			false, false);
	CHECK(back.followTail);
}

TEST_CASE("terminal follow: typing re-pins and jumps to the prompt",
	"[unit][editor][terminal]")
{
	// typing while scrolled up in history re-pins AND jumps to the bottom now
	const TerminalFollowVerdict typed =
		decide(/*following*/false, /*atBottom*/false, /*grew*/false,
			/*scrolledAway*/false, /*selecting*/false, /*sentInput*/true);
	CHECK(typed.pinToBottom);
	CHECK(typed.followTail);

	// even if a scroll-up gesture and input coincide, input wins (jump to prompt)
	const TerminalFollowVerdict both =
		decide(true, false, false, /*scrolledAway*/true, false, /*input*/true);
	CHECK(both.pinToBottom);
	CHECK(both.followTail);
}

TEST_CASE("terminal follow: an active selection freezes the pin",
	"[unit][editor][terminal]")
{
	// selecting + content grew: NO scroll (text must not slide under the pointer)
	// and the pin state is FROZEN at what it was coming in
	const TerminalFollowVerdict selPinned =
		decide(/*following*/true, true, /*grew*/true, false,
			/*selecting*/true, false);
	CHECK_FALSE(selPinned.pinToBottom);
	CHECK(selPinned.followTail);	// frozen true

	const TerminalFollowVerdict selUnpinned =
		decide(/*following*/false, false, /*grew*/true, false,
			/*selecting*/true, false);
	CHECK_FALSE(selUnpinned.pinToBottom);
	CHECK_FALSE(selUnpinned.followTail);	// frozen false

	// selection outranks even a coincident input, so the drag stays stable
	const TerminalFollowVerdict selInput =
		decide(true, true, true, false, /*selecting*/true, /*input*/true);
	CHECK_FALSE(selInput.pinToBottom);
}

// ---- the input queue in front of the pty ---------------------------------
namespace
{
	//! a sink standing in for a real terminal: its input buffer holds only
	//! `space` more bytes (a tty's is about a kilobyte) and refills only when
	//! the child reads - modelled by the test setting `space` again.
	struct FakeChild
	{
		std::string	received;
		std::size_t	space = 0;		//!< free input space right now (0 = full)
		bool		broken = false;
	};

	std::ptrdiff_t fakeSink(void* context, char const* data, std::size_t len)
	{
		FakeChild& child = *static_cast<FakeChild*>(context);
		if (child.broken)
		{
			return -1;
		}
		const std::size_t take = (child.space < len) ? child.space : len;
		child.received.append(data, take);
		child.space -= take;
		return static_cast<std::ptrdiff_t>(take);
	}
}

TEST_CASE("terminal input queue: a burst larger than one hand-over keeps its tail",
	"[unit][editor][terminal]")
{
	// the bug this guards: a paste is bigger than the terminal's input queue, so
	// the first hand-over places only part of it. Dropping the rest strands the
	// receiving app mid-sequence - a bracketed paste whose closing marker never
	// arrives swallows every later keystroke, the interrupt included.
	TerminalInputQueue queue;
	FakeChild child;
	child.space = 4;	// room for four bytes, then full until the child reads

	const std::string paste = "\x1b[200~PASTED-TEXT\x1b[201~";
	CHECK(queue.push(paste.data(), paste.size()));
	CHECK(queue.drain(&fakeSink, &child));
	// the child took what it could; the remainder is still OURS to deliver
	CHECK(child.received.size() == 4);
	CHECK(queue.pending() == paste.size() - 4);

	// keep offering it (the per-frame flush) as the child reads
	for (int i = 0; i < 100 && queue.pending() > 0; ++i)
	{
		child.space = 4;
		CHECK(queue.drain(&fakeSink, &child));
	}
	CHECK(queue.pending() == 0);
	CHECK(child.received == paste);	// in order, closing marker included

	// a following write reuses the drained buffer and still arrives whole
	child.space = 8;
	CHECK(queue.push("\x03", 1));
	CHECK(queue.drain(&fakeSink, &child));
	CHECK(queue.pending() == 0);
	CHECK(child.received == paste + "\x03");
}

TEST_CASE("terminal input queue: a full child stalls without losing a byte",
	"[unit][editor][terminal]")
{
	TerminalInputQueue queue;
	FakeChild child;
	child.space = 0;	// its input is full - it accepts nothing right now

	CHECK(queue.push("abc", 3));
	CHECK(queue.drain(&fakeSink, &child));
	CHECK(child.received.empty());
	CHECK(queue.pending() == 3);

	// a later write appends BEHIND the stalled bytes - order is the contract
	// (the interrupt code a user sends next must arrive after, never instead of,
	// what is already queued)
	CHECK(queue.push("\x03", 1));
	CHECK(queue.pending() == 4);
	child.space = 64;
	CHECK(queue.drain(&fakeSink, &child));
	CHECK(queue.pending() == 0);
	CHECK(child.received == std::string("abc\x03", 4));
}

TEST_CASE("terminal input queue: capacity refuses instead of losing a fragment",
	"[unit][editor][terminal]")
{
	TerminalInputQueue queue(8);
	FakeChild child;
	child.space = 0;

	CHECK(queue.push("12345678", 8));
	CHECK(queue.pending() == 8);
	// one more byte would exceed the cap: refused WHOLE, nothing half-queued
	CHECK_FALSE(queue.push("9", 1));
	CHECK(queue.pending() == 8);
	// draining makes room again
	child.space = 8;
	CHECK(queue.drain(&fakeSink, &child));
	CHECK(queue.pending() == 0);
	CHECK(queue.push("9", 1));
}

TEST_CASE("terminal input queue: a broken pipe is reported",
	"[unit][editor][terminal]")
{
	TerminalInputQueue queue;
	FakeChild child;
	child.broken = true;

	CHECK(queue.push("hello", 5));
	CHECK_FALSE(queue.drain(&fakeSink, &child));	// the child is gone
	queue.clear();
	CHECK(queue.pending() == 0);
}

TEST_CASE("terminal scroll max tracks a growing content height",
	"[unit][editor][terminal]")
{
	// content shorter than the view pins at the top (max 0)
	CHECK(terminalScrollMax(/*lines*/10, /*cellH*/16.0f, /*view*/400.0f) == 0.0f);
	// once content exceeds the view the max is content - view, and grows with it
	const float a = terminalScrollMax(30, 16.0f, 400.0f);	// 480 - 400
	const float b = terminalScrollMax(60, 16.0f, 400.0f);	// 960 - 400
	CHECK(a == 80.0f);
	CHECK(b == 560.0f);
	CHECK(b > a);	// the pin target tracks the growing tail
}
