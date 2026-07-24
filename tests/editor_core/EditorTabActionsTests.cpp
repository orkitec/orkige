/**************************************************************
	created:	2026/07/24 at 12:30
	filename: 	EditorTabActionsTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec
***************************************************************/
#include "EditorTabActions.h"
#include "EditorTextDiagnostics.h"

#include <catch2/catch_test_macros.hpp>

using OrkigeEditor::TabAction;
using OrkigeEditor::computeTabsToClose;

TEST_CASE("tab close-set: Close closes exactly the target", "[editor][tabs]")
{
	const std::vector<bool> close = computeTabsToClose(4, 1, TabAction::Close);
	REQUIRE(close == std::vector<bool>({ false, true, false, false }));
}

TEST_CASE("tab close-set: CloseOthers keeps only the target", "[editor][tabs]")
{
	const std::vector<bool> close =
		computeTabsToClose(4, 2, TabAction::CloseOthers);
	REQUIRE(close == std::vector<bool>({ true, true, false, true }));
}

TEST_CASE("tab close-set: CloseRight closes the later siblings",
	"[editor][tabs]")
{
	const std::vector<bool> close =
		computeTabsToClose(5, 1, TabAction::CloseRight);
	REQUIRE(close == std::vector<bool>({ false, false, true, true, true }));
	// the last tab has nothing to its right
	const std::vector<bool> none =
		computeTabsToClose(3, 2, TabAction::CloseRight);
	REQUIRE(none == std::vector<bool>({ false, false, false }));
}

TEST_CASE("tab close-set: CloseAll closes the whole group", "[editor][tabs]")
{
	const std::vector<bool> close =
		computeTabsToClose(3, 0, TabAction::CloseAll);
	REQUIRE(close == std::vector<bool>({ true, true, true }));
}

TEST_CASE("tab close-set: out-of-range target and None close nothing",
	"[editor][tabs]")
{
	REQUIRE(computeTabsToClose(3, 7, TabAction::CloseAll) ==
		std::vector<bool>({ false, false, false }));
	REQUIRE(computeTabsToClose(3, 1, TabAction::None) ==
		std::vector<bool>({ false, false, false }));
	REQUIRE(computeTabsToClose(0, 0, TabAction::CloseAll).empty());
}

TEST_CASE("xml diagnostic: well-formed and blank text pass", "[editor][diag]")
{
	CHECK(OrkigeEditor::xmlDiagnostic(
		"<Scene version=\"7\"><Object id=\"a\"/></Scene>").valid);
	CHECK(OrkigeEditor::xmlDiagnostic("").valid);
	CHECK(OrkigeEditor::xmlDiagnostic("   \n\t\n").valid);
}

TEST_CASE("xml diagnostic: a broken document names its line",
	"[editor][diag]")
{
	const OrkigeEditor::TextDiagnostic verdict = OrkigeEditor::xmlDiagnostic(
		"<Scene>\n<Object>\n</Wrong>\n</Scene>");
	REQUIRE_FALSE(verdict.valid);
	// the parser anchors a mismatch on the UNCLOSED element's line (2), not
	// the offending closer - the squiggle lands where the fix belongs
	CHECK(verdict.line == 2);
	CHECK_FALSE(verdict.message.empty());
}

TEST_CASE("lua error line extraction reads the chunk-prefixed line",
	"[editor][diag]")
{
	CHECK(OrkigeEditor::luaErrorLine(
		"scripts/player.lua:12: '=' expected near 'x'",
		"scripts/player.lua") == 12);
	// the loader may wrap the chunk name in extra prefix text
	CHECK(OrkigeEditor::luaErrorLine(
		"[string \"scripts/a.lua\"]: scripts/a.lua:3: unexpected symbol",
		"scripts/a.lua") == 3);
	CHECK(OrkigeEditor::luaErrorLine("no line here", "scripts/a.lua") == 0);
	CHECK(OrkigeEditor::luaErrorLine("", "x.lua") == 0);
}
