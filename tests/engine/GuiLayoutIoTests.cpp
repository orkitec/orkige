/**************************************************************
	created:	2026/07/11 at 16:30
	filename: 	GuiLayoutIoTests.cpp
	author:		steffen.roemer
	notice:		This source file is part of orkige (orkitec Game engine)
				For the latest info, see http://www.orkitec.com/
	copyright:	(c) 2009-2026 orkitec

	Headless unit tests for the .oui declarative-layout document model
	(engine_gui/GuiLayout): parse into ordered sections/entries, the
	round-trip (parse -> serialize -> parse is stable), and honest failure on
	malformed input. No renderer, no window - pure text, so it also covers the
	ORKIGE_NOSCRIPT path (the loader must not require Lua).
***************************************************************/

#include <catch2/catch_test_macros.hpp>

#include "engine_gui/GuiLayout.h"

using namespace Orkige;

TEST_CASE("oui: parse reads sections + ordered entries", "[unit][oui]")
{
	const String text =
		"# a comment\n"
		"[Layout]\n"
		"atlas = gui_default\n"
		"design = 1280 720 0.5\n"
		"\n"
		"[ScrollView settings]\n"
		"z = 5\n"
		"anchor = stretchall\n"
		"offsets = 20 20 -20 -20\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(error.empty());
	REQUIRE(doc.sections.size() == 2);

	CHECK(doc.sections[0].type == "Layout");
	CHECK(doc.sections[0].id.empty());
	REQUIRE(doc.sections[0].find("atlas") != nullptr);
	CHECK(*doc.sections[0].find("atlas") == "gui_default");
	CHECK(*doc.sections[0].find("design") == "1280 720 0.5");

	CHECK(doc.sections[1].type == "ScrollView");
	CHECK(doc.sections[1].id == "settings");
	CHECK(*doc.sections[1].find("anchor") == "stretchall");
	CHECK(*doc.sections[1].find("offsets") == "20 20 -20 -20");
}

TEST_CASE("oui: accepts ':' and tab separators like the classic grammar",
	"[unit][oui]")
{
	const String text =
		"[Label title]\n"
		"text : Hello\n"
		"font\t24\n";
	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));
	REQUIRE(doc.sections.size() == 1);
	CHECK(*doc.sections[0].find("text") == "Hello");
	CHECK(*doc.sections[0].find("font") == "24");
}

TEST_CASE("oui: round-trips through serialize -> parse -> serialize",
	"[unit][oui]")
{
	const String text =
		"[Layout]\n"
		"atlas = gui_default\n"
		"root = safearea\n"
		"\n"
		"[DecorWidget panel]\n"
		"sprite = panel\n"
		"anchor = stretchall\n"
		"offsets = 16 16 -16 -16\n"
		"nineSlice = true\n"
		"\n"
		"[Label title]\n"
		"parent = panel\n"
		"font = 24\n"
		"text = @settings.title\n";

	GuiLayoutDoc doc;
	String error;
	REQUIRE(GuiLayoutDoc::parse(text, doc, error));

	// the serialized canonical form re-parses to an identical document
	const String canonical = doc.serialize();
	GuiLayoutDoc doc2;
	REQUIRE(GuiLayoutDoc::parse(canonical, doc2, error));
	CHECK(doc2.serialize() == canonical);

	// and the canonical form equals the (already-canonical) input
	CHECK(canonical == text);
}

TEST_CASE("oui: a key before any section fails honestly", "[unit][oui]")
{
	const String text = "atlas = gui_default\n[Label a]\ntext = x\n";
	GuiLayoutDoc doc;
	String error;
	CHECK_FALSE(GuiLayoutDoc::parse(text, doc, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("oui: an unterminated header fails honestly", "[unit][oui]")
{
	const String text = "[Label a\ntext = x\n";
	GuiLayoutDoc doc;
	String error;
	CHECK_FALSE(GuiLayoutDoc::parse(text, doc, error));
	CHECK_FALSE(error.empty());
}

TEST_CASE("oui: section.set overwrites or appends", "[unit][oui]")
{
	GuiLayoutSection section;
	section.type = "Label";
	section.id = "a";
	section.set("text", "one");
	section.set("font", "24");
	section.set("text", "two");	// overwrite the first
	REQUIRE(section.find("text") != nullptr);
	CHECK(*section.find("text") == "two");
	CHECK(section.entries.size() == 2);
}
