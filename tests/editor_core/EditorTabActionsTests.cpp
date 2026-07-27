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

TEST_CASE("omat diagnostic: a well-formed material and blank text pass",
	"[editor][diag]")
{
	CHECK(OrkigeEditor::omatDiagnostic(
		"version 1\nalbedo 1.0 1.0 1.0 1.0\nmetalness 0.15\n").valid);
	CHECK(OrkigeEditor::omatDiagnostic("").valid);
	CHECK(OrkigeEditor::omatDiagnostic("   \n\t\n").valid);
}

TEST_CASE("omat diagnostic: a broken material names its line",
	"[editor][diag]")
{
	// a duplicate directive - MaterialAsset::parse's own "line N: ..." error
	const OrkigeEditor::TextDiagnostic verdict = OrkigeEditor::omatDiagnostic(
		"version 1\nversion 1\n");
	REQUIRE_FALSE(verdict.valid);
	CHECK(verdict.line == 2);
	CHECK_FALSE(verdict.message.empty());
}

TEST_CASE("oui diagnostic: a well-formed layout and blank text pass",
	"[editor][diag]")
{
	CHECK(OrkigeEditor::ouiDiagnostic(
		"[Layout]\natlas = gui_default\nroot = fullwindow\n").valid);
	CHECK(OrkigeEditor::ouiDiagnostic("").valid);
	CHECK(OrkigeEditor::ouiDiagnostic("   \n\t\n").valid);
}

TEST_CASE("oui diagnostic: a broken layout names its line", "[editor][diag]")
{
	// a key outside any section - GuiLayoutDoc::parse's own "... on line N"
	const OrkigeEditor::TextDiagnostic verdict =
		OrkigeEditor::ouiDiagnostic("z = 2\n");
	REQUIRE_FALSE(verdict.valid);
	CHECK(verdict.line == 1);
	CHECK_FALSE(verdict.message.empty());
}

TEST_CASE("text document kind: every house extension maps as expected",
	"[editor][diag]")
{
	using OrkigeEditor::TextDocumentKind;
	using OrkigeEditor::textDocumentKindForExtension;

	// the XMLArchive family (+ XLIFF, + a bare .xml)
	for (std::string const& ext : { std::string(".oscene"),
		std::string(".oprefab"), std::string(".orkproj"),
		std::string(".orkmeta"), std::string(".olevels"),
		std::string(".oactions"), std::string(".olayers"),
		std::string(".xlf"), std::string(".xml") })
	{
		CHECK(textDocumentKindForExtension(ext) == TextDocumentKind::Xml);
	}
	// the config-text family
	for (std::string const& ext : { std::string(".oui"),
		std::string(".ogui"), std::string(".omat"), std::string(".oshape") })
	{
		CHECK(textDocumentKindForExtension(ext) ==
			TextDocumentKind::OrkigeConfig);
	}
	CHECK(textDocumentKindForExtension(".json") == TextDocumentKind::Json);
	CHECK(textDocumentKindForExtension(".jsonl") == TextDocumentKind::Json);
	// dot-optional, case-insensitive
	CHECK(textDocumentKindForExtension("OSCENE") == TextDocumentKind::Xml);
	CHECK(textDocumentKindForExtension(".OUI") ==
		TextDocumentKind::OrkigeConfig);
	// an unrecognized extension (and a source-code one this table does not
	// carry - the caller maps those directly) is PlainText
	CHECK(textDocumentKindForExtension(".cfg") == TextDocumentKind::PlainText);
	CHECK(textDocumentKindForExtension(".lua") == TextDocumentKind::PlainText);
}

TEST_CASE("text document kind sniff: prolog/tag/brace/bracket/plain/BOM",
	"[editor][diag]")
{
	using OrkigeEditor::TextDocumentKind;
	using OrkigeEditor::sniffTextDocumentKind;

	// the house XMLArchive's non-standard prolog
	CHECK(sniffTextDocumentKind(
		"<?1.0, UTF-8, yes?>\n<XMLArchive Version=\"0\"/>\n") ==
		TextDocumentKind::Xml);
	// a standard XML declaration
	CHECK(sniffTextDocumentKind(
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<Root/>\n") ==
		TextDocumentKind::Xml);
	// a bare root tag with no prolog at all
	CHECK(sniffTextDocumentKind("<Root></Root>") == TextDocumentKind::Xml);
	CHECK(sniffTextDocumentKind("{\n  \"a\": 1\n}") == TextDocumentKind::Json);
	CHECK(sniffTextDocumentKind("[1, 2, 3]") == TextDocumentKind::Json);
	CHECK(sniffTextDocumentKind("hello world") == TextDocumentKind::PlainText);
	CHECK(sniffTextDocumentKind("") == TextDocumentKind::PlainText);
	// a leading UTF-8 BOM and blank lines are tolerated before the sniff
	CHECK(sniffTextDocumentKind(std::string("\xEF\xBB\xBF") +
		"\n   \n<Root/>") == TextDocumentKind::Xml);
	CHECK(sniffTextDocumentKind(std::string("\xEF\xBB\xBF") + "  {}") ==
		TextDocumentKind::Json);
}

TEST_CASE("live-check kind: diagnostics routing per extension",
	"[editor][diag]")
{
	using OrkigeEditor::LiveCheckKind;
	using OrkigeEditor::liveCheckKindForExtension;

	CHECK(liveCheckKindForExtension(".lua") == LiveCheckKind::Lua);
	for (std::string const& ext : { std::string(".oscene"),
		std::string(".oprefab"), std::string(".orkproj"),
		std::string(".orkmeta"), std::string(".olevels"),
		std::string(".oactions"), std::string(".olayers"),
		std::string(".xlf"), std::string(".xml") })
	{
		CHECK(liveCheckKindForExtension(ext) == LiveCheckKind::Xml);
	}
	CHECK(liveCheckKindForExtension(".omat") == LiveCheckKind::Omat);
	CHECK(liveCheckKindForExtension(".oui") == LiveCheckKind::Oui);
	// no comparably cheap line-numbered parser seam - honestly diagnostic-free
	CHECK(liveCheckKindForExtension(".ogui") == LiveCheckKind::None);
	CHECK(liveCheckKindForExtension(".oshape") == LiveCheckKind::None);
	CHECK(liveCheckKindForExtension(".json") == LiveCheckKind::None);
	CHECK(liveCheckKindForExtension(".jsonl") == LiveCheckKind::None);
	CHECK(liveCheckKindForExtension(".unknown") == LiveCheckKind::None);
}
